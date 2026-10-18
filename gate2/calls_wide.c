/* The wide set of the call table: ranks 31 and up (unit gate2-calls-wide, 2026-10-10).
 *
 * Finding 21's thirty ranks are a thin sample of the two DLLs.  These entries call many more of
 * their exports with real arguments.  They are not from the finding: every row says, in its
 * `also` text and in the comment above its code, where the expected values come from.  The
 * accepted sources are
 *   (1) an independent computation in the driver from what the function is publicly documented
 *       or generally known to do (Windows semantics of the object it wraps, the C library, the
 *       names and headers of the public Source SDK as a guess at meaning, never as proof);
 *   (2) an invariant that holds whatever the implementation (a round trip, mutual exclusion,
 *       a lookup that finds what was inserted);
 *   (3) the instructions of the original, read with tools/scout/gate2_disasm.py, for exports of
 *       one to a dozen instructions whose name alone does not say what they return.
 * Nothing here was derived from the lifted C.  How to write an entry: calls.h.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rt/handle.h"
#include "rt/heap.h"
#include "rt/init.h"
#include "rt/machine.h"
#include "rt/thread.h"
#include "rt/time.h"
#include "rt/win/sync.h"
#include "calls.h"
#include "host.h"

#define BOOL8(x)        ((uint8_t)(x))        /* a guest bool is al */
#define G(fn, ...)      GUEST_CALL(c->ctx, (fn) __VA_OPT__(,) __VA_ARGS__)
#define GB(fn, ...)     BOOL8(G(fn __VA_OPT__(,) __VA_ARGS__))
#define GI(fn, ...)     ((int32_t)G(fn __VA_OPT__(,) __VA_ARGS__))
#define FAILF(...)      do { gate2_fail_text(c, __VA_ARGS__); return; } while (0)
#define T0(var, name)   EXPORT(var, c->tier0, name)
#define VS(var, name)   EXPORT(var, c->vstdlib, name)

static GUESTFN slot(uint64_t obj, unsigned n) {
    return ((const uint64_t *)(uintptr_t)*(const uint64_t *)(uintptr_t)obj)[n];
}
static const char *gstr(uint64_t p) { return (const char *)(uintptr_t)p; }
static char *gdup(const char *s) {
    char *p = gate2_mem(strlen(s) + 1);
    strcpy(p, s);
    return p;
}
static void *gfill(size_t n, int byte) {        /* guest memory filled with a guard byte */
    void *p = gate2_mem(n);
    memset(p, byte, n);
    return p;
}
#define EXPECT_STR(what, got, want) \
    do { uint64_t p_ = (got); if (!p_ || strcmp(gstr(p_), (want)) != 0) { \
        gate2_fail_text(c, "%s: got %s%.80s%s, expected \"%s\"", (what), p_ ? "\"" : "", p_ ? gstr(p_) : "NULL", p_ ? "\"" : "", (want)); return; } } while (0)

/* ---- guest-callable callbacks ------------------------------------------------------------------ */
/* Four native stubs that record their arguments and return what the entry set.  A hook, if
 * set, runs inside the callback (on whatever guest thread called it) and may call the guest. */

typedef struct Cb {
    unsigned calls;
    uint64_t a[4];                              /* rcx, rdx, r8, r9 of the last call */
    uint64_t ret;                               /* what the stub returns in rax */
    uint32_t thread;                            /* guest thread id of the last call */
    void   (*hook)(Ctx *ctx, struct Cb *cb);
    void    *user;
} Cb;
static Cb       cbs[4];
static uint64_t cb_stub[4];

static void cb_common(Ctx *ctx, unsigned i) {
    Cb *b = &cbs[i];
    for (unsigned k = 0; k < 4; k++) b->a[k] = ARG_U64(ctx, k);
    b->thread = RT_THREAD(ctx)->id;
    b->calls++;
    if (b->hook) b->hook(ctx, b);
    ctx->r[0] = b->ret;
}
HOST_FN(cb0) { cb_common(ctx, 0); }
HOST_FN(cb1) { cb_common(ctx, 1); }
HOST_FN(cb2) { cb_common(ctx, 2); }
HOST_FN(cb3) { cb_common(ctx, 3); }

void gate2_wide_before_seal(RtModule *ntdll) {
    cb_stub[0] = rt_native_stub(ntdll, "gate2_wide_cb0", cb0);
    cb_stub[1] = rt_native_stub(ntdll, "gate2_wide_cb1", cb1);
    cb_stub[2] = rt_native_stub(ntdll, "gate2_wide_cb2", cb2);
    cb_stub[3] = rt_native_stub(ntdll, "gate2_wide_cb3", cb3);
}

/* ---- two threads against one lock ---------------------------------------------------------------- */

typedef struct { GUESTFN lock, unlock; void *obj; volatile uint64_t *counter; unsigned n; } CountJob;
static void count_loop(Call *c, void *arg) {
    CountJob *j = arg;
    for (unsigned i = 0; i < j->n; i++) {
        G(j->lock, j->obj);
        uint64_t v = *j->counter;
        for (volatile int k = 0; k < 4; k++) { }        /* keep the read and the write apart */
        *j->counter = v + 1;
        G(j->unlock, j->obj);
    }
}
/* Mutual exclusion as an invariant: two threads each add 1 to a plain counter n times, every
 * addition between `lock` and `unlock`; without exclusion additions are lost. */
static bool count_two_threads(Call *c, GUESTFN lock, GUESTFN unlock, void *obj, unsigned n, const char *what) {
    static volatile uint64_t counter;
    counter = 0;
    CountJob j = { lock, unlock, obj, &counter, n };
    void *t = gate2_thread_start(c, count_loop, &j);
    if (!t) return false;
    count_loop(c, &j);
    if (!gate2_thread_finish(c, t)) return false;
    if (counter != 2 * (uint64_t)n) {
        gate2_fail_text(c, "%s: two threads each added 1 to a counter %u times under the lock; the sum is %llu", what, n, (unsigned long long)counter);
        return false;
    }
    return true;
}

typedef struct { GUESTFN lock, unlock; void *obj; volatile int got; } BlockJob;
static void block_take(Call *c, void *arg) {
    BlockJob *j = arg;
    G(j->lock, j->obj);
    j->got = 1;
    G(j->unlock, j->obj);
}
/* The calling thread holds something that must keep j->lock out.  A second thread asks for
 * j->lock; 30 ms later it must not have it; after `release` it must get it. */
static bool blocks_until(Call *c, BlockJob *j, GUESTFN release, const char *what) {
    j->got = 0;
    void *t = gate2_thread_start(c, block_take, j);
    if (!t) return false;
    usleep(30000);
    int early = j->got;
    G(release, j->obj);
    if (!gate2_thread_finish(c, t)) return false;
    if (early) { gate2_fail_text(c, "%s: the second thread got in while the first held the lock", what); return false; }
    if (!j->got) { gate2_fail_text(c, "%s: the second thread never got the lock", what); return false; }
    return true;
}

/* ---- 31. Server-frame thread and loading-thread identity --------------------------------------- */
/* Source: the names, checked against the instructions (5 to 22 each): one global holds the id
 * of the declared "server frame" thread (-1: none), another the id given to
 * SetLoadingThreadDuringVideo; the queries compare GetCurrentThreadId with them and with the
 * main thread's id.  With no server frame thread, "could do server work" is "is the main thread". */

typedef struct { GUESTFN declare, in_server, in_main_or_server, could, in_loading; uint8_t r[5]; } ServerFrame;
static void server_frame_thread(Call *c, void *arg) {
    ServerFrame *s = arg;
    s->r[0] = GB(s->could);                     /* nobody declared: not the main thread */
    s->r[4] = GB(s->in_loading);
    G(s->declare);
    s->r[1] = GB(s->in_server);
    s->r[2] = GB(s->in_main_or_server);
    s->r[3] = GB(s->could);
}

static void run_31(Call *c) {
    ServerFrame s = { .r = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE } };
    T0(declare, "DeclareCurrentThreadIsServerFrameThread");
    T0(declare_not, "DeclareCurrentThreadNotServerFrameThread");
    T0(in_server, "ThreadInServerFrameThread");
    T0(in_main_or_server, "ThreadInMainOrServerFrameThread");
    T0(could, "ThreadCouldDoServerWork");
    T0(set_loading, "SetLoadingThreadDuringVideo");
    T0(in_loading, "ThreadInMainLoadingThreadDuringVideo");
    s.declare = declare; s.in_server = in_server; s.in_main_or_server = in_main_or_server; s.could = could; s.in_loading = in_loading;
    G(declare_not);
    EXPECT("main thread, nobody declared: ThreadInServerFrameThread", GB(in_server), 0);
    EXPECT("main thread, nobody declared: ThreadInMainOrServerFrameThread", GB(in_main_or_server), 1);
    EXPECT("main thread, nobody declared: ThreadCouldDoServerWork", GB(could), 1);
    G(set_loading, RT_FIRST_THREAD_ID);
    EXPECT("ThreadInMainLoadingThreadDuringVideo after SetLoadingThreadDuringVideo(own id)", GB(in_loading), 1);
    if (!gate2_on_thread(c, server_frame_thread, &s, NULL)) return;
    EXPECT("second thread, nobody declared: ThreadCouldDoServerWork", s.r[0], 0);
    EXPECT("second thread: ThreadInMainLoadingThreadDuringVideo", s.r[4], 0);
    EXPECT("second thread after declaring: ThreadInServerFrameThread", s.r[1], 1);
    EXPECT("second thread after declaring: ThreadInMainOrServerFrameThread", s.r[2], 1);
    EXPECT("second thread after declaring: ThreadCouldDoServerWork", s.r[3], 1);
    EXPECT("main thread, the other declared: ThreadInServerFrameThread", GB(in_server), 0);
    EXPECT("main thread, the other declared: ThreadInMainOrServerFrameThread", GB(in_main_or_server), 1);
    EXPECT("main thread, the other declared: ThreadCouldDoServerWork", GB(could), 0);
    G(declare);
    EXPECT("main thread after declaring itself: ThreadInServerFrameThread", GB(in_server), 1);
    G(declare_not);
    EXPECT("after DeclareCurrentThreadNotServerFrameThread: ThreadInServerFrameThread", GB(in_server), 0);
    EXPECT("after DeclareCurrentThreadNotServerFrameThread: ThreadCouldDoServerWork", GB(could), 1);
    G(set_loading, 0);
    EXPECT("ThreadInMainLoadingThreadDuringVideo after SetLoadingThreadDuringVideo(0)", GB(in_loading), 0);
}

/* ---- 32. Thread handle helpers --------------------------------------------------------------------- */
/* Source: Windows.  Each export is a thin wrapper (1 to 16 instructions, read) of one kernel32
 * function: GetCurrentThread (the pseudo-handle -2), GetCurrentThreadId (the export named
 * ThreadThisThreadIsMain is a jump to it), SetThreadPriority / GetThreadPriority (a round
 * trip), SetThreadAffinityMask (returns the mask before), CloseHandle (TRUE once, then FALSE). */

typedef struct { GUESTFN id; uint32_t got; } IdJob;
static void id_thread(Call *c, void *arg) { IdJob *j = arg; j->got = (uint32_t)G(j->id); }

static void run_32(Call *c) {
    T0(cur, "ThreadGetCurrentHandle");
    T0(this_id, "ThreadThisThreadIsMain");
    T0(set_pri, "ThreadSetPriority");
    T0(get_pri, "ThreadGetPriority");
    T0(set_aff, "ThreadSetAffinity");
    T0(set_name, "ThreadSetDebugName");
    T0(create, "CreateSimpleThread");
    T0(join, "ThreadJoin");
    T0(release, "ReleaseThreadHandle");
    T0(fn, "Plat_GetMemPageSize");
    EXPECT("ThreadGetCurrentHandle(): the pseudo-handle", G(cur), (uint64_t)-2);
    EXPECT("ThreadThisThreadIsMain() on the first thread: its id", (uint32_t)G(this_id), RT_FIRST_THREAD_ID);
    IdJob j = { this_id, 0 };
    uint32_t id2 = 0;
    if (!gate2_on_thread(c, id_thread, &j, &id2)) return;
    EXPECT("ThreadThisThreadIsMain() on a second thread: that thread's id", j.got, id2);
    EXPECT("ThreadSetPriority(0, 1)", GB(set_pri, 0, 1), 1);
    EXPECT("ThreadGetPriority(0) after it", GI(get_pri, 0), 1);
    EXPECT("ThreadSetPriority(0, -1)", GB(set_pri, 0, S32(-1)), 1);
    EXPECT("ThreadGetPriority(0) after it", GI(get_pri, 0), -1);
    EXPECT("ThreadSetPriority(0, 0)", GB(set_pri, 0, 0), 1);
    uint64_t before = G(set_aff, 0, 1);
    EXPECT_TRUE("ThreadSetAffinity(0, 1) returns the mask before, not 0", before != 0);
    EXPECT("ThreadSetAffinity(0, mask before) returns 1", G(set_aff, 0, before), 1);
    G(set_name, 0, gdup("gate2"));              /* one instruction: ret */
    uint64_t h = G(create, fn, 0, 0);
    EXPECT_TRUE("CreateSimpleThread returns a handle", h != 0);
    EXPECT("ThreadSetPriority(handle, 2)", GB(set_pri, h, 2), 1);
    EXPECT("ThreadGetPriority(handle)", GI(get_pri, h), 2);
    EXPECT("ThreadJoin(handle)", GB(join, h, 60000), 1);
    EXPECT("ReleaseThreadHandle(handle)", GB(release, h), 1);
    EXPECT("ReleaseThreadHandle(handle) a second time", GB(release, h), 0);
}

/* ---- 33. CThreadSpinRWLock: blocking calls and the other spellings -------------------------------- */
/* Source: invariants of a reader/writer lock, with the lock word as rank 6 found it (readers
 * counted in the first dword, 0x10000 and the owner's id for a writer).  Every exported
 * spelling of each operation (const, _UnforcedInline) must behave as the one rank 6 calls. */

static void run_33(Call *c) {
    static const char *const rd[] = { "?LockForRead@CThreadSpinRWLock@@QEAAXXZ", "?LockForRead@CThreadSpinRWLock@@QEBAXXZ" };
    static const char *const wr[] = { "?LockForWrite@CThreadSpinRWLock@@QEAAXXZ", "?LockForWrite@CThreadSpinRWLock@@QEBAXXZ" };
    static const char *const try_rd[] = { "?TryLockForRead@CThreadSpinRWLock@@QEBA_NXZ", "?TryLockForRead_UnforcedInline@CThreadSpinRWLock@@QEAA_NXZ" };
    static const char *const try_wr[] = { "?TryLockForWrite@CThreadSpinRWLock@@QEBA_NXZ", "?TryLockForWrite_UnforcedInline@CThreadSpinRWLock@@QEAA_NXZ" };
    T0(ctor, "??0CThreadSpinRWLock@@QEAA@XZ");
    T0(un_rd, "?UnlockRead@CThreadSpinRWLock@@QEBAXXZ");
    T0(un_wr, "?UnlockWrite@CThreadSpinRWLock@@QEBAXXZ");
    T0(is_rd, "?IsLockedForRead@CThreadSpinRWLock@@QEAA_NXZ");
    T0(is_wr, "?IsLockedForWrite@CThreadSpinRWLock@@QEAA_NXZ");
    T0(assign, "??4CThreadSpinRWLock@@QEAAAEAV0@AEBV0@@Z");
    uint32_t *lock = gfill(16, 0xEE);
    G(ctor, lock);
    for (int i = 0; i < 2; i++) {
        EXPORT(lock_rd, c->tier0, rd[i]);
        EXPORT(lock_wr, c->tier0, wr[i]);
        EXPORT(t_rd, c->tier0, try_rd[i]);
        EXPORT(t_wr, c->tier0, try_wr[i]);
        G(lock_rd, lock);
        EXPECT("LockForRead: readers", lock[0], 1);
        EXPECT("TryLockForRead with one reader", GB(t_rd, lock), 1);
        EXPECT("two readers", lock[0], 2);
        EXPECT("IsLockedForRead", GB(is_rd, lock), 1);
        EXPECT("IsLockedForWrite with readers", GB(is_wr, lock), 0);
        EXPECT("TryLockForWrite with readers", GB(t_wr, lock), 0);
        G(un_rd, lock);
        G(un_rd, lock);
        EXPECT("after UnlockRead twice", lock[0], 0);
        G(lock_wr, lock);
        EXPECT("LockForWrite: the lock word", lock[0], 0x10000);
        EXPECT("LockForWrite: the owner", lock[1], RT_FIRST_THREAD_ID);
        EXPECT("IsLockedForWrite", GB(is_wr, lock), 1);
        EXPECT("TryLockForRead while write-locked", GB(t_rd, lock), 0);
        G(un_wr, lock);
        EXPECT("after UnlockWrite", lock[0], 0);
        EXPECT("TryLockForWrite on a free lock", GB(t_wr, lock), 1);
        EXPECT("IsLockedForWrite after TryLockForWrite", GB(is_wr, lock), 1);
        G(un_wr, lock);
    }
    EXPECT("the bytes after the 8 of the lock", lock[2], 0xEEEEEEEEu);
    /* operator=: a copy of the 8 bytes, returning the destination. */
    uint32_t *copy = gfill(16, 0xEE);
    lock[0] = 3;
    EXPECT("operator= returns the destination", G(assign, copy, lock), (uint64_t)(uintptr_t)copy);
    EXPECT("operator= copies the lock word", copy[0], 3);
    EXPECT("operator= leaves what follows", copy[2], 0xEEEEEEEEu);
}

/* ---- 34. CThreadSpinRWLock between two threads ------------------------------------------------- */
/* Source: invariants.  A writer excludes a writer (no lost additions); a writer keeps a reader
 * out until it unlocks, and readers keep a writer out.  The contended paths are the exported
 * SpinLockForRead and SpinLockForWrite, which the short calls jump to. */

static void run_34(Call *c) {
    T0(ctor, "??0CThreadSpinRWLock@@QEAA@XZ");
    T0(lock_rd, "?LockForRead@CThreadSpinRWLock@@QEAAXXZ");
    T0(un_rd, "?UnlockRead@CThreadSpinRWLock@@QEAAXXZ");
    T0(lock_wr, "?LockForWrite@CThreadSpinRWLock@@QEAAXXZ");
    T0(un_wr, "?UnlockWrite@CThreadSpinRWLock@@QEAAXXZ");
    T0(spin_rd, "?SpinLockForRead@CThreadSpinRWLock@@QEAAXXZ");
    T0(spin_wr, "?SpinLockForWrite@CThreadSpinRWLock@@QEAAXXZ");
    uint32_t *lock = gfill(16, 0xEE);
    G(ctor, lock);
    if (!count_two_threads(c, lock_wr, un_wr, lock, 20000, "LockForWrite / UnlockWrite")) return;
    EXPECT("the lock word after the two writers", lock[0], 0);
    if (!count_two_threads(c, spin_wr, un_wr, lock, 5000, "SpinLockForWrite / UnlockWrite")) return;
    EXPECT("the lock word after the two spinning writers", lock[0], 0);
    BlockJob reader = { lock_rd, un_rd, lock, 0 }, writer = { lock_wr, un_wr, lock, 0 }, spin_reader = { spin_rd, un_rd, lock, 0 };
    G(lock_wr, lock);
    if (!blocks_until(c, &reader, un_wr, "a reader against a writer")) return;
    G(lock_wr, lock);
    if (!blocks_until(c, &spin_reader, un_wr, "SpinLockForRead against a writer")) return;
    G(lock_rd, lock);
    if (!blocks_until(c, &writer, un_rd, "a writer against a reader")) return;
    EXPECT("the lock word at the end", lock[0], 0);
}

/* ---- 35. CThreadRWLock ---------------------------------------------------------------------------- */
/* Source: invariants of a reader/writer lock; nothing is assumed about the object but that
 * 0x100 bytes hold it (the constructor is 18 instructions and the guard after it is checked). */

static void run_35(Call *c) {
    T0(ctor, "??0CThreadRWLock@@QEAA@XZ");
    T0(dtor, "??1CThreadRWLock@@QEAA@XZ");
    T0(lock_rd, "?LockForRead@CThreadRWLock@@QEAAXXZ");
    T0(lock_rd_c, "?LockForRead@CThreadRWLock@@QEBAXXZ");
    T0(un_rd, "?UnlockRead@CThreadRWLock@@QEAAXXZ");
    T0(un_rd_c, "?UnlockRead@CThreadRWLock@@QEBAXXZ");
    T0(lock_wr, "?LockForWrite@CThreadRWLock@@QEAAXXZ");
    T0(lock_wr_c, "?LockForWrite@CThreadRWLock@@QEBAXXZ");
    T0(un_wr, "?UnlockWrite@CThreadRWLock@@QEAAXXZ");
    T0(un_wr_c, "?UnlockWrite@CThreadRWLock@@QEBAXXZ");
    uint8_t *lock = gfill(0x200, 0xEE);
    G(ctor, lock);
    for (int i = 0x100; i < 0x200; i++) EXPECT("a byte 0x100 or more after the start of the object", lock[i], 0xEE);
    G(lock_rd, lock); G(lock_rd_c, lock);       /* two readers at once */
    G(un_rd, lock);   G(un_rd_c, lock);
    G(lock_wr, lock); G(un_wr, lock);
    G(lock_wr_c, lock); G(un_wr_c, lock);
    if (!count_two_threads(c, lock_wr, un_wr, lock, 20000, "LockForWrite / UnlockWrite")) return;
    BlockJob reader = { lock_rd, un_rd, lock, 0 }, writer = { lock_wr_c, un_wr_c, lock, 0 };
    G(lock_wr, lock);
    if (!blocks_until(c, &reader, un_wr, "a reader against a writer")) return;
    G(lock_rd, lock);
    if (!blocks_until(c, &writer, un_rd, "a writer against a reader")) return;
    G(lock_rd, lock); G(un_rd, lock);           /* still usable */
    G(dtor, lock);
}

/* ---- 36. CThreadMutex: the other spellings, and two threads ---------------------------------------- */
/* Source: the object is a Windows CRITICAL_SECTION (rank 11), whose RecursionCount and
 * OwningThread fields are documented in winnt.h; mutual exclusion as an invariant. */

static void run_36(Call *c) {
    T0(ctor, "??0CThreadMutex@@QEAA@XZ");
    T0(dtor, "??1CThreadMutex@@QEAA@XZ");
    T0(lock_c, "?Lock@CThreadMutex@@QEBAXXZ");
    T0(unlock_c, "?Unlock@CThreadMutex@@QEBAXXZ");
    T0(try_c, "?TryLock@CThreadMutex@@QEBA_NXZ");
    T0(lock_s, "?LockSilent@CThreadMutex@@QEAAXXZ");
    T0(unlock_s, "?UnlockSilent@CThreadMutex@@QEAAXXZ");
    T0(owned, "?AssertOwnedByCurrentThread@CThreadMutex@@QEAA_NXZ");
    T0(lock, "?Lock@CThreadMutex@@QEAAXXZ");
    T0(unlock, "?Unlock@CThreadMutex@@QEAAXXZ");
    CRITICAL_SECTION *m = gfill(0x40, 0xEE);
    G(ctor, m);
    G(lock_c, m);
    EXPECT("Lock() const: recursion count", m->RecursionCount, 1);
    EXPECT("Lock() const: owner", (uint64_t)m->OwningThread, RT_FIRST_THREAD_ID);
    EXPECT("TryLock() const on the owner", GB(try_c, m), 1);
    G(lock_s, m);
    EXPECT("LockSilent: recursion count", m->RecursionCount, 3);
    EXPECT("AssertOwnedByCurrentThread (a release build answers true)", GB(owned, m), 1);
    G(unlock_s, m); G(unlock_c, m); G(unlock_c, m);
    EXPECT("after three unlocks: recursion count", m->RecursionCount, 0);
    EXPECT("after three unlocks: owner", (uint64_t)m->OwningThread, 0);
    if (!count_two_threads(c, lock, unlock, m, 20000, "Lock / Unlock")) return;
    if (!count_two_threads(c, lock_s, unlock_s, m, 5000, "LockSilent / UnlockSilent")) return;
    BlockJob other = { lock_c, unlock_c, m, 0 };
    G(lock, m);
    if (!blocks_until(c, &other, unlock, "Lock() const against Lock()")) return;
    G(dtor, m);
}

/* ---- 37. CThreadFullMutex ----------------------------------------------------------------------- */
/* Source: Windows mutex semantics (CreateMutex, WaitForSingleObject, ReleaseMutex): owned by a
 * thread, recursive, ReleaseMutex fails for a thread that does not own it.  The class is a
 * CThreadSyncObject (handle first), so the exported CThreadSyncObject::Wait applies to it. */

typedef struct { GUESTFN wait, release; void *obj; uint8_t r[3]; } FullMutexJob;
static void full_mutex_thread(Call *c, void *arg) {
    FullMutexJob *j = arg;
    j->r[0] = GB(j->release, j->obj);           /* not the owner */
    j->r[1] = GB(j->wait, j->obj, 0);
    j->r[2] = j->r[1] ? GB(j->release, j->obj) : 0xEE;
}

static void run_37(Call *c) {
    T0(ctor, "??0CThreadFullMutex@@QEAA@_NPEBD@Z");
    T0(closure, "??_FCThreadFullMutex@@QEAAXXZ");
    T0(dtor, "??1CThreadFullMutex@@QEAA@XZ");
    T0(lock, "?Lock@CThreadFullMutex@@QEAAXXZ");
    T0(lock_ms, "?Lock@CThreadFullMutex@@QEAAXI@Z");
    T0(unlock, "?Unlock@CThreadFullMutex@@QEAAXXZ");
    T0(release, "?Release@CThreadFullMutex@@QEAA_NXZ");
    T0(owned, "?AssertOwnedByCurrentThread@CThreadFullMutex@@QEAA_NXZ");
    T0(trace, "?SetTrace@CThreadFullMutex@@QEAAX_N@Z");
    T0(wait, "?Wait@CThreadSyncObject@@QEAA_NI@Z");
    uint64_t *m = gfill(0x40, 0xEE), *m2 = gfill(0x40, 0xEE);
    G(ctor, m, 0, 0);
    EXPECT_TRUE("the constructor made a handle", m[0] != 0 && m[0] != 0xEEEEEEEEEEEEEEEEull);
    EXPECT("the bytes after the 16 of the object", m[2], 0xEEEEEEEEEEEEEEEEull);
    FullMutexJob j = { wait, release, m, { 0xEE, 0xEE, 0xEE } };
    G(lock, m);
    G(lock_ms, m, 0);                           /* recursive, with a timeout */
    EXPECT("AssertOwnedByCurrentThread (a release build answers true)", GB(owned, m), 1);
    G(trace, m, 1);
    if (!gate2_on_thread(c, full_mutex_thread, &j, NULL)) return;
    EXPECT("second thread: Release of a mutex it does not own", j.r[0], 0);
    EXPECT("second thread: Wait(0) while the first owns it", j.r[1], 0);
    EXPECT("Release (1 of 2)", GB(release, m), 1);
    G(unlock, m);                               /* the second level */
    EXPECT("Release with nothing held", GB(release, m), 0);
    if (!gate2_on_thread(c, full_mutex_thread, &j, NULL)) return;
    EXPECT("second thread: Wait(0) on the free mutex", j.r[1], 1);
    EXPECT("second thread: Release after its Wait", j.r[2], 1);
    G(dtor, m);
    G(closure, m2);                             /* the default constructor closure: (false, NULL) */
    EXPECT_TRUE("the default constructor closure made a handle", m2[0] != 0 && m2[0] != 0xEEEEEEEEEEEEEEEEull);
    EXPECT("a mutex made without initial ownership is free: Wait(0)", GB(wait, m2, 0), 1);
    EXPECT("Release after it", GB(release, m2), 1);
    G(dtor, m2);
    /* Created with initial ownership: the creating thread owns it. */
    G(ctor, m, 1, 0);
    EXPECT("made with initial ownership: Release", GB(release, m), 1);
    EXPECT("made with initial ownership: Release again", GB(release, m), 0);
    G(dtor, m);
}

/* ---- 38. CThreadEvent: auto-reset, named, CheckNamedEvent ------------------------------------- */
/* Source: Windows event semantics (an auto-reset event is consumed by one successful wait; a
 * name makes the object findable by OpenEvent).  The three results of CheckNamedEvent are the
 * public header's (threadtools.h: 0 the event does not exist, 1 not signalled, 2 signalled),
 * taken as a guess and confirmed by the three cases being told apart in that order. */

static void run_38(Call *c) {
    T0(ctor, "??0CThreadEvent@@QEAA@_N@Z");
    T0(ctor_named, "??0CThreadEvent@@QEAA@PEBD_N1@Z");
    T0(closure, "??_FCThreadEvent@@QEAAXXZ");
    T0(dtor, "??1CThreadEvent@@QEAA@XZ");
    T0(set, "?Set@CThreadEvent@@QEAA_NXZ");
    T0(reset, "?Reset@CThreadEvent@@QEAA_NXZ");
    T0(check, "?Check@CThreadEvent@@QEAA_NXZ");
    T0(wait, "?Wait@CThreadEvent@@QEAA_NI@Z");
    T0(check_named, "?CheckNamedEvent@CThreadEvent@@SA?AW4NamedEventResult_t@@PEBDI@Z");
    uint64_t *ev = gfill(0x20, 0xEE);
    G(ctor, ev, 0);                             /* auto-reset */
    EXPECT("auto-reset: Check on a new event", GB(check, ev), 0);
    EXPECT("auto-reset: Set", GB(set, ev), 1);
    EXPECT("auto-reset: Set again", GB(set, ev), 1);
    EXPECT("auto-reset: Check after Set", GB(check, ev), 1);
    EXPECT("auto-reset: Check again (the first one consumed it)", GB(check, ev), 0);
    G(set, ev);
    EXPECT("auto-reset: Wait(1000) after Set", GB(wait, ev, 1000), 1);
    EXPECT("auto-reset: Wait(0) after that", GB(wait, ev, 0), 0);
    G(dtor, ev);
    memset(ev, 0xEE, 0x20);
    G(closure, ev);                             /* the default constructor closure */
    EXPECT_TRUE("the closure made a handle", ev[0] != 0 && ev[0] != 0xEEEEEEEEEEEEEEEEull);
    EXPECT("closure: Check on a new event", GB(check, ev), 0);
    G(dtor, ev);
    memset(ev, 0xEE, 0x20);
    char *name = gdup("gate2-wide-event"), *other = gdup("gate2-wide-no-such-event");
    G(ctor_named, ev, name, 1, 1);              /* (name, initially set, manual reset) */
    EXPECT_TRUE("the named constructor made a handle", ev[0] != 0 && ev[0] != 0xEEEEEEEEEEEEEEEEull);
    EXPECT("named, initially set: Check", GB(check, ev), 1);
    EXPECT("named, manual reset: Check again", GB(check, ev), 1);
    EXPECT("CheckNamedEvent(name, 0) while set", GI(check_named, name, 0), 2);
    EXPECT("Reset", GB(reset, ev), 1);
    EXPECT("CheckNamedEvent(name, 0) while reset", GI(check_named, name, 0), 1);
    EXPECT("CheckNamedEvent(a name nobody made, 0)", GI(check_named, other, 0), 0);
    /* What the code does, found when this entry first expected "no such event" here: the named
     * constructor leaves the object's "I made this handle" byte clear, so the destructor closes
     * nothing, and CheckNamedEvent never closes the handle it opens.  The event outlives the
     * object.  That is the original's behaviour, not ours. */
    uint64_t h = ev[0];
    G(dtor, ev);
    EXPECT("CheckNamedEvent(name, 0) after the destructor: the event is still there", GI(check_named, name, 0), 1);
    EXPECT("the handle is still open after the destructor: CloseHandle", (uint64_t)rt_handle_close((HANDLE)(uintptr_t)h), 1);
}

/* ---- 39. WaitForMultiple, four spellings ------------------------------------------------------- */
/* Source: WaitForMultipleObjects as documented: the lowest index that is signalled, or
 * WAIT_TIMEOUT (0x102); with bWaitAll, WAIT_OBJECT_0 only when every object is signalled.
 * The array is of pointers in one overload and of the 16-byte objects themselves in the other
 * (the mangled names say which). */

static void run_39(Call *c) {
    static const char *const by_ptr[] = { "?WaitForMultiple@CThreadSyncObject@@SAIHPEAPEAV1@_NI@Z", "?WaitForMultiple@CThreadEvent@@SAIHPEAPEAV1@_NI@Z" };
    static const char *const by_val[] = { "?WaitForMultiple@CThreadSyncObject@@SAIHPEAV1@_NI@Z", "?WaitForMultiple@CThreadEvent@@SAIHPEAV1@_NI@Z" };
    T0(ctor, "??0CThreadEvent@@QEAA@_N@Z");
    T0(dtor, "??1CThreadEvent@@QEAA@XZ");
    T0(set, "?Set@CThreadEvent@@QEAA_NXZ");
    T0(reset, "?Reset@CThreadEvent@@QEAA_NXZ");
    uint64_t *ev = gate2_mem(3 * 16), *ptr = gate2_mem(3 * 8);
    for (int i = 0; i < 3; i++) { G(ctor, &ev[2 * i], 1); ptr[i] = (uint64_t)(uintptr_t)&ev[2 * i]; }   /* manual reset */
    for (int v = 0; v < 4; v++) {
        EXPORT(f, c->tier0, v < 2 ? by_ptr[v] : by_val[v - 2]);
        void *arr = v < 2 ? (void *)ptr : (void *)ev;
        for (int i = 0; i < 3; i++) G(reset, &ev[2 * i]);
        EXPECT("none set, any: WAIT_TIMEOUT", (uint32_t)G(f, 3, arr, 0, 0), 0x102);
        G(set, &ev[4]);
        EXPECT("the third set, any", (uint32_t)G(f, 3, arr, 0, 0), 2);
        EXPECT("the third set, any, of the first two only", (uint32_t)G(f, 2, arr, 0, 0), 0x102);
        G(set, &ev[2]);
        EXPECT("the second and third set, any: the lower index", (uint32_t)G(f, 3, arr, 0, 0), 1);
        EXPECT("the second and third set, all", (uint32_t)G(f, 3, arr, 1, 0), 0x102);
        G(set, &ev[0]);
        EXPECT("all set, all", (uint32_t)G(f, 3, arr, 1, 1000), 0);
        EXPECT("all set, any", (uint32_t)G(f, 3, arr, 0, 1000), 0);
    }
    for (int i = 0; i < 3; i++) G(dtor, &ev[2 * i]);
}

/* ---- 40. CThreadSyncObject, the base class -------------------------------------------------------- */
/* Source: the mangled names (operator! returns bool, GetHandle the handle) and the invariant
 * that an object which holds no handle says so and one that holds an event's does not. */

static void run_40(Call *c) {
    T0(ctor, "??0CThreadSyncObject@@IEAA@XZ");
    T0(dtor, "??1CThreadSyncObject@@QEAA@XZ");
    T0(op_not, "??7CThreadSyncObject@@QEBA_NXZ");
    T0(get_handle, "?GetHandle@CThreadSyncObject@@QEBAQEAXXZ");
    T0(usable, "?AssertUseable@CThreadSyncObject@@IEAAXXZ");
    T0(ev_ctor, "??0CThreadEvent@@QEAA@_N@Z");
    uint64_t *o = gfill(0x20, 0xEE);
    EXPECT("the constructor returns the object", G(ctor, o), (uint64_t)(uintptr_t)o);
    EXPECT("a new object: GetHandle", G(get_handle, o), 0);
    EXPECT("a new object: operator!", GB(op_not, o), 1);
    EXPECT("the bytes after the 16 of the object", o[2], 0xEEEEEEEEEEEEEEEEull);
    G(usable, o);
    G(dtor, o);                                 /* nothing to close */
    G(ev_ctor, o, 1);
    EXPECT_TRUE("an event: GetHandle is its handle", G(get_handle, o) == o[0] && o[0] != 0);
    EXPECT("an event: operator!", GB(op_not, o), 0);
    uint64_t h = o[0];
    G(dtor, o);                                 /* the base destructor closes the handle */
    EXPECT("the handle after the base destructor: CloseHandle", (uint64_t)rt_handle_close((HANDLE)(uintptr_t)h), 0);
}

/* ---- 41. CThreadLocalBase ---------------------------------------------------------------------------- */
/* Source: Windows TLS semantics: a slot reads NULL on a thread that never set it, and each
 * thread reads back its own value. */

typedef struct { GUESTFN get, set; void *obj; uint64_t first, second; } TlsJob;
static void tls_thread(Call *c, void *arg) {
    TlsJob *j = arg;
    j->first = G(j->get, j->obj);
    G(j->set, j->obj, 0x2222);
    j->second = G(j->get, j->obj);
}

static void run_41(Call *c) {
    T0(ctor, "??0CThreadLocalBase@GenericThreadLocals@@QEAA@XZ");
    T0(dtor, "??1CThreadLocalBase@GenericThreadLocals@@QEAA@XZ");
    T0(get, "?Get@CThreadLocalBase@GenericThreadLocals@@QEBAPEAXXZ");
    T0(set, "?Set@CThreadLocalBase@GenericThreadLocals@@QEAAXPEAX@Z");
    T0(assign, "??4CThreadLocalBase@GenericThreadLocals@@QEAAAEAV01@AEBV01@@Z");
    uint32_t *a = gfill(0x20, 0xEE), *b = gfill(0x20, 0xEE), *copy = gfill(0x20, 0xEE);
    G(ctor, a);
    G(ctor, b);
    EXPECT_TRUE("two objects have two different slots", a[0] != b[0] && a[0] < 1088 && b[0] < 1088);
    EXPECT("the bytes after the first 8 of the object", a[2], 0xEEEEEEEEu);
    EXPECT("Get before any Set", G(get, a), 0);
    G(set, a, 0x1111);
    G(set, b, 0x3333);
    EXPECT("Get after Set(0x1111)", G(get, a), 0x1111);
    EXPECT("Get of the other object", G(get, b), 0x3333);
    EXPECT("the runtime's TLS slot holds the value", rt_tls_get(c->ctx, a[0]), 0x1111);
    TlsJob j = { get, set, a, 0xEE, 0xEE };
    if (!gate2_on_thread(c, tls_thread, &j, NULL)) return;
    EXPECT("second thread: Get before it set anything", j.first, 0);
    EXPECT("second thread: Get after its Set", j.second, 0x2222);
    EXPECT("first thread: Get afterwards", G(get, a), 0x1111);
    EXPECT("operator= returns the destination", G(assign, copy, a), (uint64_t)(uintptr_t)copy);
    EXPECT("a copy reads the same slot", G(get, copy), 0x1111);
    G(dtor, a);
    G(dtor, b);
}

/* ---- 42. CThread, not started ------------------------------------------------------------------- */
/* Source: the export table (the constructor must install the exported vtable, GetThreadProc
 * must return the exported ThreadProc), the round trip SetName / GetName, and what a thread
 * object that was never started must say: no handle, no id, not alive. */

static void run_42(Call *c) {
    T0(ctor, "??0CThread@@QEAA@XZ");
    T0(dtor, "??1CThread@@UEAA@XZ");
    T0(vtable, "??_7CThread@@6B@");
    T0(thread_proc, "?ThreadProc@CThread@@KAKPEAX@Z");
    T0(get_proc, "?GetThreadProc@CThread@@IEAAP6AKPEAX@ZXZ");
    T0(get_name, "?GetName@CThread@@QEAAPEBDXZ");
    T0(set_name, "?SetName@CThread@@QEAAXPEBD@Z");
    T0(get_id, "?GetThreadID@CThread@@IEBAIXZ");
    T0(get_handle, "?GetThreadHandle@CThread@@QEAAPEAUV_ThreadHandle_t__@@XZ");
    T0(get_handle_c, "?GetThreadHandle@CThread@@IEBAQEAUV_ThreadHandle_t__@@XZ");
    T0(is_alive, "?IsAlive@CThread@@QEAA_NXZ");
    T0(init, "?Init@CThread@@IEAA_NXZ");
    T0(run, "?Run@CThread@@MEAAHXZ");
    T0(on_exit, "?OnExit@CThread@@IEAAXXZ");
    uint64_t *t = gfill(0x400, 0xEE);
    EXPECT("the constructor returns the object", G(ctor, t), (uint64_t)(uintptr_t)t);
    EXPECT("the object's vptr against the exported vtable", t[0], vtable);
    for (int i = 0x40; i < 0x80; i++) EXPECT("a qword 0x200 or more after the start of the object", t[i], 0xEEEEEEEEEEEEEEEEull);
    EXPECT("GetThreadProc() against the exported ThreadProc", G(get_proc, t), thread_proc);
    EXPECT("GetThreadID() before Start", (uint32_t)G(get_id, t), 0);
    EXPECT("GetThreadHandle() before Start", G(get_handle, t), 0);
    EXPECT("GetThreadHandle() const before Start", G(get_handle_c, t), 0);
    EXPECT("IsAlive() before Start", GB(is_alive, t), 0);
    G(set_name, t, gdup("gate2-worker"));
    EXPECT_STR("GetName() after SetName", G(get_name, t), "gate2-worker");
    EXPECT("the base class's Init()", GB(init, t), 1);
    G(on_exit, t);
    G(run, t);                                  /* the base class's Run: two instructions, it returns */
    EXPECT("IsAlive() after the base class's Run was called directly", GB(is_alive, t), 0);
    G(dtor, t);                                 /* the exported destructor; ranks 43 and 78 go through the vtable */
    EXPECT("the destructor leaves the exported vtable in place", t[0], vtable);
}

/* ---- 43. CThread::Start with a Run of the driver's ------------------------------------------- */
/* Source: what a thread class must do: Start runs the object's virtual Run on a new thread,
 * Join waits for it, GetResult is what Run returned, and inside Run GetCurrentCThread is the
 * object.  The driver's object is a CThread with a copy of the exported vtable in which the
 * slot that holds the exported Run is replaced by a native stub.  PRIORITY 0 is
 * THREAD_PRIORITY_NORMAL, the public header's default on Windows. */

typedef struct { Call *c; GUESTFN current, get_id, is_alive; uint64_t cur; uint32_t id; uint8_t alive; } RunSeen;
static void run_hook(Ctx *ctx, Cb *cb) {
    RunSeen *s = cb->user;
    uint64_t self = cb->a[0];
    s->cur = GUEST_CALL(ctx, s->current);
    s->id = (uint32_t)GUEST_CALL(ctx, s->get_id, self);
    s->alive = BOOL8(GUEST_CALL(ctx, s->is_alive, self));
    cb->ret = 1234;
}

static void run_43(Call *c) {
    T0(ctor, "??0CThread@@QEAA@XZ");
    T0(vtable, "??_7CThread@@6B@");
    T0(run, "?Run@CThread@@MEAAHXZ");
    T0(start, "?Start@CThread@@UEAA_N_KW4ThreadPriorityEnum_t@1@@Z");
    T0(join, "?Join@CThread@@QEAA_NI@Z");
    T0(get_result, "?GetResult@CThread@@QEAAHXZ");
    T0(is_alive, "?IsAlive@CThread@@QEAA_NXZ");
    T0(get_id, "?GetThreadID@CThread@@IEBAIXZ");
    T0(get_handle, "?GetThreadHandle@CThread@@QEAAPEAUV_ThreadHandle_t__@@XZ");
    T0(current, "?GetCurrentCThread@CThread@@SAPEAV1@XZ");
    T0(get_pri, "?GetPriority@CThread@@QEBA?AW4ThreadPriority_e@@XZ");
    T0(set_name, "?SetName@CThread@@QEAAXPEBD@Z");
    enum { SLOTS = 12 };
    uint64_t *t = gfill(0x400, 0xEE), *vt = gate2_mem(SLOTS * 8);
    const uint64_t *orig = (const uint64_t *)(uintptr_t)vtable;
    int run_slot = -1, start_slot = -1;
    for (int i = 0; i < SLOTS; i++) {
        vt[i] = orig[i];
        if (orig[i] == run && run_slot < 0) run_slot = i;
        if (orig[i] == start && start_slot < 0) start_slot = i;
    }
    if (run_slot < 0 || start_slot < 0) FAILF("the exported Run and Start are not both among the first %d slots of the exported vtable (Run %d, Start %d)", SLOTS, run_slot, start_slot);
    RunSeen seen = { .c = c, .current = current, .get_id = get_id, .is_alive = is_alive, .cur = 0xEE, .alive = 0xEE };
    cbs[0] = (Cb){ .hook = run_hook, .user = &seen };
    vt[run_slot] = cb_stub[0];
    G(ctor, t);
    t[0] = (uint64_t)(uintptr_t)vt;
    G(set_name, t, gdup("gate2-run"));
    unsigned before = rt_thread_count();
    EXPECT("GetCurrentCThread() on a thread that is no CThread", G(current), 0);
    EXPECT("Start(0, 0)", GB(slot((uint64_t)(uintptr_t)t, (unsigned)start_slot), t, 0, 0), 1);
    EXPECT("Join(60000)", GB(join, t, 60000), 1);
    EXPECT("calls of Run", cbs[0].calls, 1);
    EXPECT("Run's this", cbs[0].a[0], (uint64_t)(uintptr_t)t);
    EXPECT_TRUE("Run ran on another guest thread", cbs[0].thread != 0 && cbs[0].thread != RT_FIRST_THREAD_ID);
    EXPECT("GetCurrentCThread() inside Run", seen.cur, (uint64_t)(uintptr_t)t);
    EXPECT("GetThreadID() inside Run against the thread Run ran on", seen.id, cbs[0].thread);
    EXPECT("IsAlive() inside Run", seen.alive, 1);
    EXPECT("GetResult() after Join: what Run returned", GI(get_result, t), 1234);
    EXPECT("IsAlive() after Join", GB(is_alive, t), 0);
    EXPECT("GetCurrentCThread() on the first thread afterwards", G(current), 0);
    (void)get_handle; (void)get_pri;
    for (int i = 0; i < 200 && rt_thread_count() != before; i++) usleep(1000);
    EXPECT("guest threads after the join", rt_thread_count(), before);
    G(slot((uint64_t)(uintptr_t)t, 0), t, 0);   /* destructor */
}

/* ---- 44. CThread statics and thread-id allocation helpers ------------------------------------ */
/* Source: Windows (Sleep waits at least the time asked; SwitchToThread returns), and
 * CalcStackDepth by its three instructions: the stack base stored in the object minus the
 * argument, which for an object never started is 0 - p. */

static void run_44(Call *c) {
    T0(sleep, "?Sleep@CThread@@SAXI@Z");
    T0(yield, "?Yield@CThread@@SAXXZ");
    T0(ctor, "??0CThread@@QEAA@XZ");
    T0(depth, "?CalcStackDepth@CThread@@QEAA_KPEAX@Z");
    T0(suspended, "?IsSuspended@CThread@@QEAA_NXZ");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    G(sleep, 25);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    if (ms < 24.0 || ms > 5000.0) FAILF("CThread::Sleep(25) returned after %.1f ms", ms);
    for (int i = 0; i < 100; i++) G(yield);
    uint64_t *t = gfill(0x400, 0xEE);
    G(ctor, t);
    uint64_t d1 = G(depth, t, 0x1000), d2 = G(depth, t, 0x3000);
    EXPECT("CalcStackDepth(p) - CalcStackDepth(p + 0x2000)", d1 - d2, 0x2000);
    EXPECT("IsSuspended() on a thread never started", GB(suspended, t), 0);
}

/* ---- the logging system ----------------------------------------------------------------------- */
/* Signatures: the public tier0/logging.h, as a guess, checked where it mattered against the
 * instructions (RegisterLoggingChannel takes name, tags function, flags, severity, colour;
 * GetChannel returns the address of a 0x38-byte record; IsChannelEnabled compares the severity
 * with the record's dword at +8).  Severities: 0 message, 1 warning, 2 assert, 3 error. */

enum { CH_ID = 0, CH_FLAGS = 4, CH_MIN_SEVERITY = 8, CH_COLOUR = 12, CH_NAME = 16 };   /* the channel record, by the public header */

static Gate2Listener wide_rec;
/* Forget what the listener has received, so that the next message is text[0]. */
static void rec_clear(void) { wide_rec.calls = 0; memset(wide_rec.text, 0, sizeof wide_rec.text); }

/* ---- 45. Logging channels: register, find, enumerate ------------------------------------------ */
/* Source: invariants.  A channel registered under a new name gets the next id and is found by
 * that name; registering the name again gives the same channel; walking First/Next visits
 * every id once and ends with -1; the record GetChannel returns carries the id and the name. */

static void run_45(Call *c) {
    T0(reg, "LoggingSystem_RegisterLoggingChannel");
    T0(find, "LoggingSystem_FindChannel");
    T0(count, "LoggingSystem_GetChannelCount");
    T0(first, "LoggingSystem_GetFirstChannelID");
    T0(next, "LoggingSystem_GetNextChannelID");
    T0(get, "LoggingSystem_GetChannel");
    int32_t n0 = GI(count);
    EXPECT("FindChannel before registering", GI(find, gdup("gate2_wide_a")), -1);
    int32_t a = GI(reg, gdup("gate2_wide_a"), 0, 0, 0, 0x11223344);
    EXPECT("the id of a new channel: the count before", a, n0);
    EXPECT("GetChannelCount after one registration", GI(count), n0 + 1);
    int32_t b = GI(reg, gdup("gate2_wide_b"), 0, 0, 1, 0);
    EXPECT("the id of the next new channel", b, n0 + 1);
    EXPECT("registering the first name again", GI(reg, gdup("gate2_wide_a"), 0, 0, 0, 0x11223344), a);
    EXPECT("GetChannelCount after that", GI(count), n0 + 2);
    EXPECT("FindChannel(gate2_wide_a)", GI(find, gdup("gate2_wide_a")), a);
    EXPECT("FindChannel(GATE2_WIDE_B)", GI(find, gdup("GATE2_WIDE_B")), b);
    int32_t id = GI(first), seen = 0;
    for (; id != -1 && seen <= n0 + 2; id = GI(next, S32(id)), seen++)
        EXPECT("the id the walk is at", id, seen);
    EXPECT("ids visited by GetFirstChannelID / GetNextChannelID", seen, n0 + 2);
    const uint8_t *ra = (const uint8_t *)(uintptr_t)G(get, S32(a)), *rb = (const uint8_t *)(uintptr_t)G(get, S32(b));
    EXPECT("the distance between two channels' records", (uint64_t)(rb - ra), 0x38);
    EXPECT("record: id", *(const int32_t *)(ra + CH_ID), a);
    EXPECT("record: minimum severity of a", *(const int32_t *)(ra + CH_MIN_SEVERITY), 0);
    EXPECT("record: minimum severity of b", *(const int32_t *)(rb + CH_MIN_SEVERITY), 1);
    EXPECT("record: colour of a", *(const uint32_t *)(ra + CH_COLOUR), 0x11223344);
    EXPECT_TRUE("record: name of a", strcmp((const char *)ra + CH_NAME, "gate2_wide_a") == 0);
    EXPECT_TRUE("record: name of b", strcmp((const char *)rb + CH_NAME, "gate2_wide_b") == 0);
}

/* ---- 46. Logging channels: colour, flags, spew level, tags ------------------------------------ */
/* Source: round trips, and "enabled" as the header describes it: a channel is enabled for a
 * severity at or above its minimum.  Tags are added from inside the function the registration
 * is given, which the registration must therefore call exactly once. */

static void tags_hook(Ctx *ctx, Cb *cb) {
    GUEST_CALL(ctx, (GUESTFN)(uintptr_t)cb->user, gdup("gate2tag"));
    GUEST_CALL(ctx, (GUESTFN)(uintptr_t)cb->user, gdup("gate2tag2"));
}

static void run_46(Call *c) {
    T0(reg, "LoggingSystem_RegisterLoggingChannel");
    T0(add_tag, "LoggingSystem_AddTagToCurrentChannel");
    T0(has_tag, "LoggingSystem_HasTag");
    T0(enabled, "LoggingSystem_IsChannelEnabled");
    T0(get_colour, "LoggingSystem_GetChannelColor");
    T0(set_colour, "LoggingSystem_SetChannelColor");
    T0(get_flags, "LoggingSystem_GetChannelFlags");
    T0(set_flags, "LoggingSystem_SetChannelFlags");
    T0(set_level, "LoggingSystem_SetChannelSpewLevel");
    T0(set_level_name, "LoggingSystem_SetChannelSpewLevelByName");
    T0(set_level_tag, "LoggingSystem_SetChannelSpewLevelByTag");
    cbs[0] = (Cb){ .hook = tags_hook, .user = (void *)(uintptr_t)add_tag };
    int32_t a = GI(reg, gdup("gate2_wide_tagged"), cb_stub[0], 2, 1, 0x0A0B0C0D);
    int32_t b = GI(reg, gdup("gate2_wide_plain"), 0, 0, 0, 0);
    EXPECT("calls of the tags function during registration", cbs[0].calls, 1);
    EXPECT("HasTag(a, gate2tag)", GB(has_tag, S32(a), gdup("gate2tag")), 1);
    EXPECT("HasTag(a, gate2tag2)", GB(has_tag, S32(a), gdup("gate2tag2")), 1);
    EXPECT("HasTag(a, nosuchtag)", GB(has_tag, S32(a), gdup("nosuchtag")), 0);
    EXPECT("HasTag(b, gate2tag)", GB(has_tag, S32(b), gdup("gate2tag")), 0);
    EXPECT("GetChannelColor(a)", (uint32_t)G(get_colour, S32(a)), 0x0A0B0C0D);
    G(set_colour, S32(a), 0x01020304);
    EXPECT("GetChannelColor after SetChannelColor", (uint32_t)G(get_colour, S32(a)), 0x01020304);
    EXPECT("GetChannelFlags(a)", (uint32_t)G(get_flags, S32(a)), 2);
    G(set_flags, S32(a), 1);
    EXPECT("GetChannelFlags after SetChannelFlags", (uint32_t)G(get_flags, S32(a)), 1);
    EXPECT("GetChannelFlags(b) is untouched", (uint32_t)G(get_flags, S32(b)), 0);
    for (int sev = 0; sev <= 3; sev++) {
        EXPECT("IsChannelEnabled(a, severity), minimum 1", GB(enabled, S32(a), sev), sev >= 1);
        EXPECT("IsChannelEnabled(b, severity), minimum 0", GB(enabled, S32(b), sev), 1);
    }
    G(set_level, S32(b), 3);
    for (int sev = 0; sev <= 3; sev++) EXPECT("IsChannelEnabled(b, severity) after SetChannelSpewLevel(b, 3)", GB(enabled, S32(b), sev), sev >= 3);
    G(set_level_name, gdup("gate2_wide_plain"), 2);
    for (int sev = 0; sev <= 3; sev++) EXPECT("IsChannelEnabled(b, severity) after SetChannelSpewLevelByName(.., 2)", GB(enabled, S32(b), sev), sev >= 2);
    G(set_level_tag, gdup("gate2tag2"), 0);
    for (int sev = 0; sev <= 3; sev++) {
        EXPECT("IsChannelEnabled(a, severity) after SetChannelSpewLevelByTag(gate2tag2, 0)", GB(enabled, S32(a), sev), 1);
        EXPECT("IsChannelEnabled(b, severity): b has no such tag", GB(enabled, S32(b), sev), sev >= 2);
    }
    EXPECT("IsChannelEnabled(-1, 3)", GB(enabled, S32(-1), 3), 0);
    EXPECT("IsChannelEnabled(an id never given, 3)", GB(enabled, 100000, 3), 0);
}

/* ---- 47. The logging state stack ---------------------------------------------------------------- */
/* Source: the public header's description: a pushed state that is cleared has no listeners, a
 * pushed state that is not cleared keeps them, ResetCurrentLoggingState drops them, and a pop
 * brings the state before the push back.  Checked as: does a message reach the listener. */

static void run_47(Call *c) {
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(push, "LoggingSystem_PushLoggingState");
    T0(pop, "LoggingSystem_PopLoggingState");
    T0(reset, "LoggingSystem_ResetCurrentLoggingState");
    T0(msg, "Msg");
    G(reg_listener, gate2_listener(&wide_rec));
    char *text = gdup("m\n");
    G(msg, text);
    EXPECT("listener registered: messages received", wide_rec.calls, 1);
    G(push, 0, 1);                              /* (thread local: no, clear: yes) */
    G(msg, text);
    EXPECT("in a pushed, cleared state", wide_rec.calls, 1);
    G(pop, 0);
    G(msg, text);
    EXPECT("after the pop", wide_rec.calls, 2);
    G(push, 0, 0);                              /* a copy of the state */
    G(msg, text);
    EXPECT("in a pushed copy of the state", wide_rec.calls, 3);
    G(reset);
    G(msg, text);
    EXPECT("after ResetCurrentLoggingState in the copy", wide_rec.calls, 3);
    G(pop, 0);
    G(msg, text);
    EXPECT("after the second pop", wide_rec.calls, 4);
}

/* ---- 48. The four logging entry points ------------------------------------------------------------ */
/* Source: the text is what printf makes of the arguments (the host's snprintf), and the
 * listener's context must carry the channel, the severity and the colour that were passed. */

static void run_48(Call *c) {
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(reg, "LoggingSystem_RegisterLoggingChannel");
    T0(log, "LoggingSystem_Log");
    T0(log_colour, "?LoggingSystem_Log@@YA?AW4LoggingResponse_t@@HW4LoggingSeverity_t@@VColor@@PEBDZZ");
    T0(log_direct, "LoggingSystem_LogDirect");
    T0(log_v, "LoggingSystem_LogV");
    G(reg_listener, gate2_listener(&wide_rec));
    int32_t ch = GI(reg, gdup("gate2_wide_log"), 0, 0, 0, 0x00FF00FF);
    char want[64];
    snprintf(want, sizeof want, "a=%d b=%s\n", -17, "str");
    EXPECT("LoggingSystem_Log returns 0 (continue)", GI(log, S32(ch), 0, gdup("a=%d b=%s\n"), S32(-17), gdup("str")), 0);
    EXPECT("LoggingSystem_Log: messages received", wide_rec.calls, 1);
    if (strcmp(wide_rec.text[0], want) != 0) FAILF("LoggingSystem_Log: the listener received \"%.80s\"", wide_rec.text[0]);
    EXPECT("LoggingSystem_Log: channel", wide_rec.channel[0], (uint32_t)ch);
    EXPECT("LoggingSystem_Log: severity", wide_rec.severity[0], 0);
    EXPECT("the variadic overload with a colour returns 0", GI(log_colour, S32(ch), 1, 0x80402010, gdup("a=%d b=%s\n"), S32(-17), gdup("str")), 0);
    EXPECT("with a colour: messages received", wide_rec.calls, 2);
    if (strcmp(wide_rec.text[1], want) != 0) FAILF("with a colour: the listener received \"%.80s\"", wide_rec.text[1]);
    EXPECT("with a colour: severity", wide_rec.severity[1], 1);
    EXPECT("with a colour: the colour in the context", wide_rec.colour[1], 0x80402010);
    EXPECT("LoggingSystem_LogDirect returns 0", GI(log_direct, S32(ch), 1, 0x01020304, gdup("direct %d\n")), 0);
    EXPECT("LogDirect: messages received", wide_rec.calls, 3);
    if (strcmp(wide_rec.text[2], "direct %d\n") != 0) FAILF("LogDirect: the listener received \"%.80s\" (the text is not a format)", wide_rec.text[2]);
    EXPECT("LogDirect: channel", wide_rec.channel[2], (uint32_t)ch);
    EXPECT("LogDirect: severity", wide_rec.severity[2], 1);
    EXPECT("LogDirect: the colour in the context", wide_rec.colour[2], 0x01020304);
    uint64_t *va = gate2_mem(32);               /* a va_list on x64 is a pointer to 8-byte slots */
    va[0] = S32(-17);
    va[1] = (uint64_t)(uintptr_t)gdup("str");
    EXPECT("LoggingSystem_LogV returns 0", GI(log_v, S32(ch), 0, gdup("a=%d b=%s\n"), va), 0);
    EXPECT("LogV: messages received", wide_rec.calls, 4);
    if (strcmp(wide_rec.text[3], want) != 0) FAILF("LogV: the listener received \"%.80s\"", wide_rec.text[3]);
    EXPECT("calls of the listener's slot 0", wide_rec.slot0_calls, 0);
}

/* ---- 49. The logging response policy ----------------------------------------------------------- */
/* Source: the public header as a guess (a policy is an object with a virtual function that is
 * given the message's context and returns the response), confirmed by the call arriving once
 * per message with that context, and by the log call returning what it returned.  The entry
 * first expected the call in slot 0, as the public header declares it; the code calls slot 1
 * (after its loop over the listeners, with the same context), so that is what is expected now.
 * A response of 1 means "break into the debugger", which the code does only when
 * Plat_IsInDebugSession says one is attached; here none is, and the 1 comes back. */

typedef struct { uint32_t context[4]; } PolicySeen;
static void policy_hook(Ctx *ctx, Cb *cb) {
    (void)ctx;
    memcpy(((PolicySeen *)cb->user)->context, (const void *)(uintptr_t)cb->a[1], 16);
}

static void run_49(Call *c) {
    T0(set_policy, "LoggingSystem_SetLoggingResponsePolicy");
    T0(reg, "LoggingSystem_RegisterLoggingChannel");
    T0(log, "LoggingSystem_Log");
    PolicySeen seen = { { 0xEE, 0xEE, 0xEE, 0xEE } };
    uint64_t *vt = gate2_mem(32), *obj = gate2_mem(8);
    cbs[1] = (Cb){ .hook = policy_hook, .user = &seen };        /* returns 0: continue */
    cbs[2] = cbs[3] = (Cb){ 0 };
    vt[0] = cb_stub[2]; vt[1] = cb_stub[1]; vt[2] = vt[3] = cb_stub[3];
    obj[0] = (uint64_t)(uintptr_t)vt;
    int32_t ch = GI(reg, gdup("gate2_wide_policy"), 0, 0, 0, 0);
    G(set_policy, obj);
    EXPECT("LoggingSystem_Log (a warning) returns what the policy returned", GI(log, S32(ch), 1, gdup("p\n")), 0);
    EXPECT("calls of the policy's slot 1", cbs[1].calls, 1);
    EXPECT("calls of other slots of the policy", cbs[2].calls + cbs[3].calls, 0);
    EXPECT("the policy's this", cbs[1].a[0], (uint64_t)(uintptr_t)obj);
    EXPECT("the context the policy saw: channel", seen.context[0], (uint32_t)ch);
    EXPECT("the context the policy saw: severity", seen.context[2], 1);
    cbs[1].ret = 1;
    EXPECT("LoggingSystem_Log (a message) with a policy that answers 1", GI(log, S32(ch), 0, gdup("q\n")), 1);
    EXPECT("calls of the policy's slot 1 after a second message", cbs[1].calls, 2);
    EXPECT("the context the policy saw: severity of the second", seen.context[2], 0);
    EXPECT("calls of other slots of the policy at the end", cbs[2].calls + cbs[3].calls, 0);
}

/* ---- 50. DevMsg, DevWarning, ConDMsg, ConColorMsg ------------------------------------------------ */
/* Source: printf (the host's snprintf) for the text.  Whether a developer message is shown
 * depends on a level this entry does not set, so each call may deliver nothing; what is
 * delivered must be the formatted text, and a warning may be preceded by the "Error: [n] "
 * prefix rank 26 found.  ConColorMsg's colour must arrive in the listener's context. */

static void run_50(Call *c) {
    static const char *const names[] = { "?DevMsg@@YAXPEBDZZ", "?DevWarning@@YAXPEBDZZ", "ConDMsg" };
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(con_colour, "?ConColorMsg@@YAXAEBVColor@@PEBDZZ");
    G(reg_listener, gate2_listener(&wide_rec));
    char want[64];
    snprintf(want, sizeof want, "x=%d y=%s z=%u\n", -5, "why", 4000000000u);
    static char note[160];
    size_t at = 0;
    for (unsigned i = 0; i < 3; i++) {
        EXPORT(f, c->tier0, names[i]);
        rec_clear();
        G(f, gdup("x=%d y=%s z=%u\n"), S32(-5), gdup("why"), 4000000000u);
        unsigned n = wide_rec.calls;
        if (n > 2) FAILF("%s: the listener was called %u times for one message", names[i], n);
        if (n == 2 && strncmp(wide_rec.text[0], "Error: [", 8) != 0) FAILF("%s: two messages, the first is \"%.60s\"", names[i], wide_rec.text[0]);
        if (n && strcmp(wide_rec.text[n - 1], want) != 0) FAILF("%s: the listener received \"%.80s\"", names[i], wide_rec.text[n - 1]);
        at += (size_t)snprintf(note + at, sizeof note - at, "%s%s %s", i ? ", " : "", i == 0 ? "DevMsg" : i == 1 ? "DevWarning" : "ConDMsg", n ? "delivered" : "not shown");
    }
    uint32_t *colour = gate2_mem(4);
    *colour = 0x7F103050;
    rec_clear();
    G(con_colour, colour, gdup("x=%d y=%s z=%u\n"), S32(-5), gdup("why"), 4000000000u);
    EXPECT("ConColorMsg: messages received", wide_rec.calls, 1);
    if (strcmp(wide_rec.text[0], want) != 0) FAILF("ConColorMsg: the listener received \"%.80s\"", wide_rec.text[0]);
    EXPECT("ConColorMsg: the colour in the context", wide_rec.colour[0], 0x7F103050);
    gate2_note(c, note);
}

/* ---- 51. printf through Msg: integers, strings, characters ----------------------------------- */
/* Source: the C standard's printf, computed by the host's snprintf with the same arguments,
 * for conversions on which the Visual Studio 2012 library and C99 agree.  Three more are
 * Microsoft's own, as documented: %I64d is a 64-bit integer, %p is 16 upper-case hexadecimal
 * digits with no prefix, and %ls / %S take a UTF-16 string. */

static void run_51(Call *c) {
    static const struct { const char *fmt; int kind; long long a, b, d; } cases[] = {
        { "%d|%5d|%-5d|%05d\n", 'i', 42, 42, -42 },      { "%+d|% d|%.3d\n", 'i', 7, 7, 7 },
        { "%x|%X|%#x\n", 'i', 0xbeef, 0xBEEF, 255 },     { "%08x|%o|%i\n", 'i', 0x1234, 8, -2147483647 - 1 },
        { "%u|%u|%c\n", 'i', -1, 0, 'Z' },               { "%3c|%-3c|%%\n", 'i', 'a', 'b', 0 },
        { "%lld|%llu\n", 'q', -9000000000000000000LL, -1, 0 }, { "%llx|%20lld\n", 'q', 0x123456789abcdef0LL, 1234567890123LL, 0 },
        { "%hd|%hu|%ld\n", 'i', 70000, 70000, -3 },      { "%*d|%-*d|\n", '*', 6, 12, 4 },
    };
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(msg, "Msg");
    G(reg_listener, gate2_listener(&wide_rec));
    char want[200];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        long long a = cases[i].a, b = cases[i].b, d = cases[i].d;
        rec_clear();
        if (cases[i].kind == 'q')      snprintf(want, sizeof want, cases[i].fmt, a, b);
        else if (cases[i].kind == '*') snprintf(want, sizeof want, cases[i].fmt, (int)a, (int)b, (int)d, (int)b);
        else                           snprintf(want, sizeof want, cases[i].fmt, (int)a, (int)b, (int)d);
        if (cases[i].kind == '*') G(msg, gdup(cases[i].fmt), S32(a), S32(b), S32(d), S32(b));
        else                      G(msg, gdup(cases[i].fmt), (uint64_t)a, (uint64_t)b, (uint64_t)d);
        if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], want) != 0)
            FAILF("Msg(\"%.*s\", ...): %u messages, \"%.80s\"; snprintf gives \"%.80s\"", (int)strcspn(cases[i].fmt, "\n"), cases[i].fmt, wide_rec.calls, wide_rec.text[0], want);
    }
    rec_clear();
    snprintf(want, sizeof want, "%s|%8s|%-8s|%.2s|%.*s\n", "abc", "abc", "abc", "abc", 1, "xyz");
    G(msg, gdup("%s|%8s|%-8s|%.2s|%.*s\n"), gdup("abc"), gdup("abc"), gdup("abc"), gdup("abc"), 1, gdup("xyz"));
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], want) != 0) FAILF("string conversions: %u messages, \"%.80s\"; snprintf gives \"%.80s\"", wide_rec.calls, wide_rec.text[0], want);
    rec_clear();
    G(msg, gdup("%I64d|%I64x|%p\n"), (uint64_t)-1234567890123456789LL, 0xFEDCBA9876543210ull, 0xABCDull);
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], "-1234567890123456789|fedcba9876543210|000000000000ABCD\n") != 0)
        FAILF("%%I64d, %%I64x, %%p: %u messages, \"%.80s\"", wide_rec.calls, wide_rec.text[0]);
    uint16_t *wide = gate2_mem(16);
    wide[0] = 'w'; wide[1] = 'i'; wide[2] = 'd'; wide[3] = 'e';
    rec_clear();
    G(msg, gdup("%ls|%S|%.2ls\n"), wide, wide, wide);
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], "wide|wide|wi\n") != 0) FAILF("%%ls, %%S: %u messages, \"%.80s\"", wide_rec.calls, wide_rec.text[0]);
    /* A long line: 300 characters through %s; the listener keeps 199. */
    char *big = gate2_mem(301);
    memset(big, 'L', 300);
    rec_clear();
    G(msg, gdup("%s"), big);
    EXPECT("a 300-character message: messages received", wide_rec.calls, 1);
    EXPECT("a 300-character message: the first 199 are what was sent", strlen(wide_rec.text[0]) == 199 && strspn(wide_rec.text[0], "L") == 199, 1);
}

/* ---- 52. printf through Msg: floating point --------------------------------------------------- */
/* Source: the host's snprintf, with the one known difference of the Visual Studio 2012
 * library applied to it: an exponent has at least three digits (1.5e+003).  The values are
 * chosen so that no conversion is a rounding tie and none needs more than 15 significant
 * digits, where the two libraries are known to differ.  Infinity prints as 1.#INF00 there.
 * A variadic double is passed as its 8 bytes in the argument's integer slot (Windows x64). */

static void three_digit_exponent(char *s) {
    for (char *p = s; *p; p++)
        if ((*p == 'e' || *p == 'E') && (p[1] == '+' || p[1] == '-') && p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9' && !(p[4] >= '0' && p[4] <= '9')) {
            memmove(p + 3, p + 2, strlen(p + 2) + 1);
            p[2] = '0';
            p += 4;
        }
}
static uint64_t dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

static void run_52(Call *c) {
    static const struct { const char *fmt; double a, b, d; } cases[] = {
        { "%f|%f|%f\n", 3.14159265, -0.5, 0.0 },          { "%.2f|%.1f|%.0f\n", 3.14159265, 1234.5678, 2.7 },
        { "%10.3f|%-10.3f|%+.3f\n", 2.71828, 2.71828, 2.71828 }, { "%e|%E|%.3e\n", 1234.5678, 0.00012345, -6.02214e23 },
        { "%g|%g|%g\n", 100000.0, 1000000.0, 0.0001 },    { "%g|%G|%.3g\n", 0.00001234, 1.5e-10, 1234567.0 },
        { "%f|%.3f|%g\n", 123456789.125, 1e15, 1e15 },    { "%.10f|%.6e|%g\n", 0.1, 0.1, 0.1 },
        { "%f|%e|%g\n", 1e-7, 1e-7, 1e-7 },               { "%08.2f|%#.0f|% f\n", -3.14159, 5.2, 1.25 },
        { "%f|%g|%.4f\n", 65536.0, 65536.0, 1.0 / 3.0 },  { "%g|%g|%g\n", 2.5, -0.0, 1e100 },
    };
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(msg, "Msg");
    G(reg_listener, gate2_listener(&wide_rec));
    char want[200];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        rec_clear();
        snprintf(want, sizeof want, cases[i].fmt, cases[i].a, cases[i].b, cases[i].d);
        three_digit_exponent(want);
        G(msg, gdup(cases[i].fmt), dbits(cases[i].a), dbits(cases[i].b), dbits(cases[i].d));
        if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], want) != 0)
            FAILF("Msg(\"%.*s\", %.17g, %.17g, %.17g): %u messages, \"%.80s\"; expected \"%.80s\"", (int)strcspn(cases[i].fmt, "\n"), cases[i].fmt,
                  cases[i].a, cases[i].b, cases[i].d, wide_rec.calls, wide_rec.text[0], want);
    }
    rec_clear();
    G(msg, gdup("%f|%f\n"), dbits(INFINITY), dbits(-INFINITY));
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], "1.#INF00|-1.#INF00\n") != 0) FAILF("infinity: %u messages, \"%.80s\"", wide_rec.calls, wide_rec.text[0]);
    /* Mixed: an integer, a double, a string, a double beyond the fourth argument (on the stack). */
    rec_clear();
    snprintf(want, sizeof want, "%d %.3f %s %d %.2f %.1f\n", 7, 0.125, "mid", -8, 99.5, -0.26);
    G(msg, gdup("%d %.3f %s %d %.2f %.1f\n"), 7, dbits(0.125), gdup("mid"), S32(-8), dbits(99.5), dbits(-0.26));
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], want) != 0) FAILF("mixed arguments: %u messages, \"%.80s\"; expected \"%.80s\"", wide_rec.calls, wide_rec.text[0], want);
}

/* ---- 53. The error counters --------------------------------------------------------------------- */
/* Source: rank 26 (Warning prefixes its text with "Error: [n] ", n a running count) and the
 * instructions of ErrorCountReset (6: it says "Resetting error counter." through Msg and
 * zeroes that count and the one ActionableErrorCountGet returns). */

static void run_53(Call *c) {
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(warning, "Warning");
    T0(reset, "ErrorCountReset");
    T0(actionable, "ActionableErrorCountGet");
    G(reg_listener, gate2_listener(&wide_rec));
    char *w = gdup("w\n");
    G(warning, w);
    G(warning, w);
    EXPECT("two warnings: messages received", wide_rec.calls, 4);
    if (strcmp(wide_rec.text[0], "Error: [1] ") != 0 || strcmp(wide_rec.text[2], "Error: [2] ") != 0)
        FAILF("the prefixes of two warnings are \"%.40s\" and \"%.40s\"", wide_rec.text[0], wide_rec.text[2]);
    rec_clear();
    G(reset);
    EXPECT("ErrorCountReset: messages received", wide_rec.calls, 1);
    if (strcmp(wide_rec.text[0], "Resetting error counter.\n") != 0) FAILF("ErrorCountReset said \"%.60s\"", wide_rec.text[0]);
    EXPECT("ActionableErrorCountGet() after the reset", GI(actionable), 0);
    rec_clear();
    G(warning, w);
    if (wide_rec.calls != 2 || strcmp(wide_rec.text[0], "Error: [1] ") != 0) FAILF("the prefix of the first warning after the reset is \"%.40s\" (%u messages)", wide_rec.text[0], wide_rec.calls);
}

/* ---- 54. Print_ time stamps ---------------------------------------------------------------------- */
/* Source: the instructions (2 to 14 each): two float time stamps, each with a "set" byte;
 * Print_GetTimeStamp(float *out) gives the client's if set, else the server's if set, else
 * returns false and writes nothing.  The float argument travels in xmm0. */

static void run_54(Call *c) {
    T0(set_client, "Print_SetClientTime");
    T0(set_server, "Print_SetServerTime");
    T0(clear_client, "Print_ClearClientTime");
    T0(clear_server, "Print_ClearServerTime");
    T0(get, "Print_GetTimeStamp");
    T0(prefix, "Print_EnableTimePrefix");
    float *out = gate2_mem(8);
    uint32_t *raw = (uint32_t *)out;
    G(clear_client); G(clear_server);
    raw[0] = raw[1] = 0xEEEEEEEEu;
    EXPECT("Print_GetTimeStamp with neither set", GB(get, out), 0);
    EXPECT("Print_GetTimeStamp with neither set writes nothing", raw[0], 0xEEEEEEEEu);
    rt_guest_call_ex(c->ctx, set_server, (RtArg[]){ RT_ARG_F(12.5f) }, 1);
    EXPECT("Print_GetTimeStamp with the server's set", GB(get, out), 1);
    EXPECT_TRUE("the server's time stamp", out[0] == 12.5f && raw[1] == 0xEEEEEEEEu);
    rt_guest_call_ex(c->ctx, set_client, (RtArg[]){ RT_ARG_F(-0.375f) }, 1);
    EXPECT("Print_GetTimeStamp with both set", GB(get, out), 1);
    EXPECT_TRUE("the client's time stamp wins", out[0] == -0.375f);
    G(clear_client);
    EXPECT("Print_GetTimeStamp after Print_ClearClientTime", GB(get, out), 1);
    EXPECT_TRUE("the server's again", out[0] == 12.5f);
    G(clear_server);
    EXPECT("Print_GetTimeStamp after both are cleared", GB(get, out), 0);
    G(prefix, 1); G(prefix, 0);                 /* stores one byte */
}

/* ---- 55. Helpers that are compiled out ----------------------------------------------------------- */
/* Source: the instructions.  Each of these exports is a single `ret` in this build (pointer
 * validation, call-stack spew switches, debug strings, profiler publishing, symbol paths).
 * Called with pointers into a guarded buffer, each must return and leave the buffer alone. */

static void run_55(Call *c) {
    static const char *const names[] = {
        "_AssertValidReadPtr", "_AssertValidReadWritePtr", "_AssertValidStringPtr", "_AssertValidWritePtr",
        "_Error_AlwaysSpewCallStack_Enable", "_Error_AlwaysSpewCallStack_Length", "_Warning_AlwaysSpewCallStack_Enable",
        "_Warning_AlwaysSpewCallStack_Length", "Plat_DebugString", "PublishAll", "PublishAllMiniProfilers",
        "SetStackTranslationSymbolSearchPath", "StackToolsNotify_LoadedLibrary", "ThreadSetDebugName",
    };
    uint8_t *buf = gfill(64, 0xC7);
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        EXPORT(f, c->tier0, names[i]);
        G(f, buf, buf + 16, 16, buf + 32);
        for (int k = 0; k < 64; k++) if (buf[k] != 0xC7) FAILF("%s wrote to the buffer it was given (byte %d)", names[i], k);
    }
}

/* ---- 56. Call-stack tools ------------------------------------------------------------------------ */
/* Source: the instructions (2 to 5 each).  This build captures and translates nothing:
 * GetCallStack and GetCallStack_Fast return 0 frames; the three address-to-name functions
 * return false and, given a buffer of at least one byte, make it the empty string;
 * TranslateStackInfo returns 0 and empties its output buffer the same way. */

static void run_56(Call *c) {
    static const char *const to_name[] = { "GetFileAndLineFromAddress", "GetModuleNameFromAddress", "GetSymbolNameFromAddress" };
    T0(get_stack, "GetCallStack");
    T0(get_stack_fast, "GetCallStack_Fast");
    T0(translate, "TranslateStackInfo");
    T0(dump, "_CCallStackStatsGatherer_Internal_DumpStatsToFile");
    uint64_t *frames = gfill(64, 0xC7);
    char *out = gfill(32, 0xC7);
    EXPECT("GetCallStack(frames, 8, 0)", GI(get_stack, frames, 8, 0), 0);
    EXPECT("GetCallStack_Fast(frames, 8, 0)", GI(get_stack_fast, frames, 8, 0), 0);
    EXPECT("the frame buffer is untouched", frames[0], 0xC7C7C7C7C7C7C7C7ull);
    for (unsigned i = 0; i < 3; i++) {
        EXPORT(f, c->tier0, to_name[i]);
        memset(out, 0xC7, 32);
        EXPECT(to_name[i], GB(f, c->tier0->base + 0x1a0c0, out, 32, frames), 0);
        EXPECT("the output buffer: first byte", (uint8_t)out[0], 0);
        EXPECT("the output buffer: second byte", (uint8_t)out[1], 0xC7);
        memset(out, 0xC7, 32);
        EXPECT(to_name[i], GB(f, c->tier0->base + 0x1a0c0, out, 0, frames), 0);
        EXPECT("a buffer of size 0 is not written", (uint8_t)out[0], 0xC7);
    }
    memset(out, 0xC7, 32);
    EXPECT("TranslateStackInfo(frames, 8, out, 32)", GI(translate, frames, 8, out, 32), 0);
    EXPECT("TranslateStackInfo: first byte of the output", (uint8_t)out[0], 0);
    EXPECT("_CCallStackStatsGatherer_Internal_DumpStatsToFile", GB(dump, 0, 0, 0), 0);
}

/* ---- 57. Callbacks the engine installs: remote error report, minidump, studio LAN ---------- */
/* Source: the instructions (2 to 10 each): three setters store a function pointer; the three
 * senders jump to it with their own arguments when it is set, and otherwise return 0 (the
 * error report: false).  The callbacks here are the driver's; nothing is sent anywhere.  The
 * senders end in a jump through a register, which the lifter must turn into a call. */

static void run_57(Call *c) {
    T0(set_report, "SetRemoteErrorReportCallback");
    T0(send_report, "SendRemoteErrorReport");
    T0(set_dump, "SetRemoteMinidumpSendCallback");
    T0(send_dump, "SendRemoteMinidump");
    T0(set_lan, "SetIsKitOutsideOfStudioLANCallback");
    T0(is_outside, "IsKitOutsideOfStudioLAN");
    cbs[0] = (Cb){ .ret = 0x1101 }; cbs[1] = (Cb){ .ret = 0x2202 }; cbs[2] = (Cb){ .ret = 0x3301 };
    EXPECT("SendRemoteErrorReport with no callback", GB(send_report, 1, 2, 3, 4, 5, 6, 7, 1, 0), 0);
    EXPECT("IsKitOutsideOfStudioLAN with no callback", G(is_outside), 0);
    G(send_dump, 1, 2, 3, 4);                   /* no callback: returns */
    G(set_report, cb_stub[0]);
    G(set_dump, cb_stub[1]);
    G(set_lan, cb_stub[2]);
    EXPECT("SendRemoteErrorReport: the callback's result", G(send_report, 0xA1, 0xA2, 0xA3, 0xA4, 5, 6, 7, 1, 0), 0x1101);
    EXPECT("SendRemoteErrorReport: calls of the callback", cbs[0].calls, 1);
    EXPECT_TRUE("SendRemoteErrorReport: the callback's first four arguments",
                cbs[0].a[0] == 0xA1 && cbs[0].a[1] == 0xA2 && cbs[0].a[2] == 0xA3 && cbs[0].a[3] == 0xA4);
    EXPECT("SendRemoteMinidump: the callback's result", G(send_dump, 0xB1, 0xB2, 0xB3, 0xB4), 0x2202);
    EXPECT("SendRemoteMinidump: calls of the callback", cbs[1].calls, 1);
    EXPECT_TRUE("SendRemoteMinidump: the callback's arguments", cbs[1].a[0] == 0xB1 && cbs[1].a[1] == 0xB2 && cbs[1].a[2] == 0xB3 && cbs[1].a[3] == 0xB4);
    EXPECT("IsKitOutsideOfStudioLAN: the callback's result", G(is_outside), 0x3301);
    EXPECT("IsKitOutsideOfStudioLAN: calls of the callback", cbs[2].calls, 1);
    G(set_report, 0); G(set_dump, 0); G(set_lan, 0);
    EXPECT("SendRemoteErrorReport after the callback was removed", GB(send_report, 1, 2, 3, 4, 5, 6, 7, 1, 0), 0);
    EXPECT("calls of the callbacks afterwards", cbs[0].calls + cbs[1].calls + cbs[2].calls, 3);
}

/* ---- random numbers ---------------------------------------------------------------------------- */

/* The generator, as finding 21 modelled it and rank 10 confirmed over 3,000 draws: the state
 * is the seed as given; a draw first replaces a state <= 0 by max(-state, 1), then
 * state = state * 48271 mod 2147483647. */
static int32_t rnd_next(int32_t *s) {
    if (*s <= 0) { int64_t v = -(int64_t)*s; *s = v < 1 || v > 0x7fffffff ? 1 : (int32_t)v; }
    *s = (int32_t)((uint64_t)*s * 48271u % 2147483647u);
    return *s;
}
/* The float draw, from the public vstdlib/random.cpp and the 23 instructions of the method:
 * (float)draw * AM, capped at RNMX, scaled into [lo, hi]; all in single precision. */
#define RND_AM   (1.0f / 2147483647.0f)
#define RND_RNMX (1.0f - 1.2e-7f)
static float rnd_unit(int32_t *s) {
    volatile float fl = (float)rnd_next(s) * RND_AM;
    return fl < RND_RNMX ? fl : RND_RNMX;
}
static float rnd_float(int32_t *s, float lo, float hi) {
    volatile float span = hi - lo, scaled = span * rnd_unit(s);
    return scaled + lo;
}
static float call_f(Call *c, GUESTFN fn, const RtArg *args, unsigned n) { return rt_guest_call_ex(c->ctx, fn, args, n).xmm0.f[0]; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ---- 58. CUniformRandomStream: GenerateRandomNumber, copies, InstallUniformRandomStream ------ */
/* Source: the generator's model (above) for the numbers; for a copy, that it continues the
 * sequence of the stream it was copied from; for Install, the public header's meaning (the
 * global functions draw from the installed stream; NULL installs the built-in one again). */

static void run_58(Call *c) {
    VS(ctor, "??0CUniformRandomStream@@QEAA@XZ");
    VS(copy_ctor, "??0CUniformRandomStream@@QEAA@AEBV0@@Z");
    VS(assign, "??4CUniformRandomStream@@QEAAAEAV0@AEBV0@@Z");
    VS(dtor, "??1CUniformRandomStream@@UEAA@XZ");
    VS(gen, "?GenerateRandomNumber@CUniformRandomStream@@AEAAHXZ");
    VS(set_seed, "?SetSeed@CUniformRandomStream@@UEAAXH@Z");
    VS(get_seed, "?GetSeed@CUniformRandomStream@@UEAAHXZ");
    VS(install, "InstallUniformRandomStream");
    VS(g_seed, "RandomSeed");
    VS(g_int, "RandomInt");
    VS(g_get, "GetCurrentRandomSeed");
    uint64_t *a = gfill(0x80, 0xEE), *b = gfill(0x80, 0xEE), *d = gfill(0x80, 0xEE);
    G(ctor, a);
    for (int k = 0; k < 3; k++) {
        static const int32_t seeds[] = { 12345, 0, -77 };
        int32_t s = seeds[k];
        G(set_seed, a, S32(s));
        for (int i = 0; i < 2000; i++) {
            int32_t want = rnd_next(&s), got = GI(gen, a);
            if (got != want) FAILF("seed %d, draw %d of GenerateRandomNumber: got %d, the model gives %d", seeds[k], i, got, want);
        }
        EXPECT("GetSeed after 2000 draws", GI(get_seed, a), s);
    }
    int32_t s = 4242;
    G(set_seed, a, S32(s));
    rnd_next(&s); G(gen, a);
    EXPECT("the copy constructor returns the new object", G(copy_ctor, b, a), (uint64_t)(uintptr_t)b);
    EXPECT("the copy's vptr", b[0], a[0]);
    EXPECT("the copy's seed", GI(get_seed, b), s);
    G(ctor, d);
    EXPECT("operator= returns the destination", G(assign, d, a), (uint64_t)(uintptr_t)d);
    int32_t next = rnd_next(&s);
    EXPECT("the next draw of the original", GI(gen, a), next);
    EXPECT("the next draw of the copy", GI(gen, b), next);
    EXPECT("the next draw of the assigned object", GI(gen, d), next);
    /* The global functions on an installed stream. */
    int32_t before = GI(g_get);
    G(set_seed, a, 999);
    G(install, a);
    EXPECT("GetCurrentRandomSeed with the object installed", GI(g_get), 999);
    s = 999;
    int32_t want = rnd_next(&s);
    EXPECT("the global RandomInt(0, 0x7ffffffe) draws from the object", GI(g_int, 0, 0x7ffffffe) , (int32_t)((uint32_t)want % 0x7fffffffu));
    EXPECT("the object's state after the global draw", GI(get_seed, a), s);
    G(install, 0);
    EXPECT("GetCurrentRandomSeed after Install(NULL): the built-in stream's again", GI(g_get), before);
    G(g_seed, 31337);
    EXPECT("RandomSeed after Install(NULL) leaves the object alone", GI(get_seed, a), s);
    G(dtor, b); G(dtor, d); G(dtor, a);
}

/* ---- 59. RandomFloat ------------------------------------------------------------------------- */
/* Source: an independent computation of the same single-precision arithmetic (model above),
 * compared bit for bit; and the two constants against the public ones.  This is the first
 * call of float arguments and a float result through a lifted function. */

static void run_59(Call *c) {
    static const float ranges[][2] = { { 0.0f, 1.0f }, { -1.0f, 1.0f }, { 10.0f, 1000.0f }, { -3.5f, -3.25f }, { 5.0f, 5.0f }, { 1.0f, 0.0f } };
    VS(ctor, "??0CUniformRandomStream@@QEAA@XZ");
    VS(set_seed, "?SetSeed@CUniformRandomStream@@UEAAXH@Z");
    VS(get_seed, "?GetSeed@CUniformRandomStream@@UEAAHXZ");
    VS(m_float, "?RandomFloat@CUniformRandomStream@@UEAAMMM@Z");
    VS(g_float, "RandomFloat");
    VS(g_seed, "RandomSeed");
    float am, rnmx;
    memcpy(&am, gstr(c->vstdlib->base + 0x5c5a4), 4);
    memcpy(&rnmx, gstr(c->vstdlib->base + 0x5c5a8), 4);
    EXPECT("the multiplier constant against 1/2147483647 as a float", fbits(am), fbits(RND_AM));
    EXPECT("the cap constant against 1 - 1.2e-7 as a float", fbits(rnmx), fbits(RND_RNMX));
    uint64_t *obj = gfill(0x80, 0xEE);
    G(ctor, obj);
    for (unsigned r = 0; r < sizeof ranges / sizeof ranges[0]; r++) {
        float lo = ranges[r][0], hi = ranges[r][1];
        int32_t s = (int32_t)(1000 + r);
        G(set_seed, obj, S32(s));
        G(g_seed, S32(s));
        for (int i = 0; i < 2000; i++) {
            float want = rnd_float(&s, lo, hi);
            float got = call_f(c, m_float, (RtArg[]){ RT_ARG_U(obj), RT_ARG_F(lo), RT_ARG_F(hi) }, 3);
            if (fbits(got) != fbits(want)) FAILF("RandomFloat(%g, %g) on the object, draw %d: got %.9g (%#x), the model gives %.9g (%#x)", lo, hi, i, got, fbits(got), want, fbits(want));
            got = call_f(c, g_float, (RtArg[]){ RT_ARG_F(lo), RT_ARG_F(hi) }, 2);
            if (fbits(got) != fbits(want)) FAILF("the global RandomFloat(%g, %g), draw %d: got %.9g, the model gives %.9g", lo, hi, i, got, want);
            float low = lo < hi ? lo : hi, high = lo < hi ? hi : lo;
            if (!(got >= low && got <= high)) FAILF("RandomFloat(%g, %g) = %.9g, outside the range", lo, hi, got);
        }
        EXPECT("GetSeed after 2000 float draws", GI(get_seed, obj), s);
    }
    /* The cap: the largest state gives (float)2147483646 * AM = 1.0f, which must come out as RNMX. */
    G(set_seed, obj, 1);
    *(int32_t *)&obj[1] = 2147483646;           /* the seed field (finding 21: +8) */
    int32_t s = 2147483646;
    float want = rnd_float(&s, 0.0f, 1.0f);
    float got = call_f(c, m_float, (RtArg[]){ RT_ARG_U(obj), RT_ARG_F(0.0f), RT_ARG_F(1.0f) }, 3);
    if (fbits(got) != fbits(want)) FAILF("RandomFloat(0, 1) from state 2147483646: got %.9g, the model gives %.9g", got, want);
}

/* ---- 60. RandomFloatExp ------------------------------------------------------------------------- */
/* Source: the same model with the unit draw raised to the exponent (public random.cpp and the
 * 33 instructions: no power is taken when the exponent is 1).  The power is the C library's
 * powf, computed here by the host's; the two libraries may differ in the last place, so one
 * unit in the last place of the unit value is allowed, and the count of such differences noted. */

static void run_60(Call *c) {
    static const float exps[] = { 1.0f, 2.0f, 0.5f, 3.7f, 0.25f };
    VS(ctor, "??0CUniformRandomStream@@QEAA@XZ");
    VS(set_seed, "?SetSeed@CUniformRandomStream@@UEAAXH@Z");
    VS(m_exp, "?RandomFloatExp@CUniformRandomStream@@UEAAMMMM@Z");
    VS(g_exp, "RandomFloatExp");
    VS(g_seed, "RandomSeed");
    uint64_t *obj = gfill(0x80, 0xEE);
    G(ctor, obj);
    unsigned off_by_one = 0, total = 0;
    uint64_t draws = 0;                         /* every draw's bits, for the comparison with the real DLL (values.h) */
    for (unsigned e = 0; e < sizeof exps / sizeof exps[0]; e++) {
        int32_t s = (int32_t)(77 + e);
        G(set_seed, obj, S32(s));
        G(g_seed, S32(s));
        for (int i = 0; i < 1000; i++, total++) {
            float unit = rnd_unit(&s);
            float p = exps[e] == 1.0f ? unit : powf(unit, exps[e]);
            float got = call_f(c, m_exp, (RtArg[]){ RT_ARG_U(obj), RT_ARG_F(0.0f), RT_ARG_F(1.0f), RT_ARG_F(exps[e]) }, 4);
            float got2 = call_f(c, g_exp, (RtArg[]){ RT_ARG_F(0.0f), RT_ARG_F(1.0f), RT_ARG_F(exps[e]) }, 3);
            if (fbits(got) != fbits(got2)) FAILF("RandomFloatExp(0, 1, %g), draw %d: the object gives %.9g and the global function %.9g", exps[e], i, got, got2);
            draws = gate2_stream(draws, fbits(got));
            int32_t d = (int32_t)fbits(got) - (int32_t)fbits(p);
            if (d < -1 || d > 1 || (d && exps[e] == 1.0f)) FAILF("RandomFloatExp(0, 1, %g), draw %d: got %.9g (%#x); powf(%.9g, %g) is %.9g (%#x)", exps[e], i, got, fbits(got), unit, exps[e], p, fbits(p));
            off_by_one += d != 0;
        }
    }
    /* Scaled into a range: (hi - lo) * unit^exp + lo, with the unit value taken from a (0, 1) call. */
    int32_t s = 5;
    G(set_seed, obj, 5);
    float u = call_f(c, m_exp, (RtArg[]){ RT_ARG_U(obj), RT_ARG_F(0.0f), RT_ARG_F(1.0f), RT_ARG_F(2.0f) }, 4);
    G(set_seed, obj, 5);
    float got = call_f(c, m_exp, (RtArg[]){ RT_ARG_U(obj), RT_ARG_F(-8.0f), RT_ARG_F(24.0f), RT_ARG_F(2.0f) }, 4);
    volatile float span = 24.0f - -8.0f, scaled = span * u;
    float want = scaled + -8.0f;
    (void)s;
    if (fbits(got) != fbits(want)) FAILF("RandomFloatExp(-8, 24, 2): got %.9g, 32 * %.9g - 8 is %.9g", got, u, want);
    gate2_value(c, "RandomFloatExp: the bits of the 5,000 draws, as one digest", draws);
    static char note[100];
    snprintf(note, sizeof note, "%u of %u draws differ from the host's powf by one unit in the last place", off_by_one, total);
    gate2_note(c, note);
}

/* ---- 61. CGaussianRandomStream and RandomGaussianFloat ------------------------------------------ */
/* Source: invariants.  The same seed gives the same sequence; 20,000 draws with mean 3 and
 * standard deviation 2 have a sample mean within 0.06 of 3 and a sample deviation within 0.06
 * of 2 (four to six standard errors); a stream attached to another uniform stream draws from
 * that one.  The method is not assumed. */

static void run_61(Call *c) {
    VS(u_ctor, "??0CUniformRandomStream@@QEAA@XZ");
    VS(u_seed, "?SetSeed@CUniformRandomStream@@UEAAXH@Z");
    VS(u_get, "?GetSeed@CUniformRandomStream@@UEAAHXZ");
    VS(ctor, "??0CGaussianRandomStream@@QEAA@PEAVIUniformRandomStream@@@Z");
    VS(closure, "??_FCGaussianRandomStream@@QEAAXXZ");
    VS(copy_ctor, "??0CGaussianRandomStream@@QEAA@AEBV0@@Z");
    VS(assign, "??4CGaussianRandomStream@@QEAAAEAV0@AEBV0@@Z");
    VS(dtor, "??1CGaussianRandomStream@@QEAA@XZ");
    VS(attach, "?AttachToStream@CGaussianRandomStream@@QEAAXPEAVIUniformRandomStream@@@Z");
    VS(m_float, "?RandomFloat@CGaussianRandomStream@@QEAAMMM@Z");
    VS(g_gauss, "RandomGaussianFloat");
    VS(g_seed, "RandomSeed");
    uint64_t *u = gfill(0x80, 0xEE), *u2 = gfill(0x80, 0xEE), *g = gfill(0x100, 0xEE), *g2 = gfill(0x100, 0xEE), *g3 = gfill(0x100, 0xEE);
    G(u_ctor, u); G(u_ctor, u2);
    G(ctor, g, u);
    enum { N = 20000 };
    static float first[64];
    double sum = 0, sum2 = 0;
    uint64_t draws = 0;                         /* every draw's bits, for the comparison with the real DLL (values.h) */
    G(u_seed, u, 2024);
    for (int i = 0; i < N; i++) {
        float x = call_f(c, m_float, (RtArg[]){ RT_ARG_U(g), RT_ARG_F(3.0f), RT_ARG_F(2.0f) }, 3);
        if (!(x > -30.0f && x < 36.0f)) FAILF("draw %d of RandomFloat(3, 2) is %.9g", i, x);
        if (i < 64) first[i] = x;
        sum += x; sum2 += (double)x * x;
        draws = gate2_stream(draws, fbits(x));
    }
    gate2_value(c, "CGaussianRandomStream::RandomFloat(3, 2): the bits of the 20,000 draws, as one digest", draws);
    double mean = sum / N, dev = sqrt(sum2 / N - mean * mean);
    if (fabs(mean - 3.0) > 0.06 || fabs(dev - 2.0) > 0.06) FAILF("20,000 draws with mean 3 and deviation 2 have sample mean %.4f and deviation %.4f", mean, dev);
    EXPECT_TRUE("the Gaussian draws took numbers from the attached uniform stream", GI(u_get, u) != 2024);
    /* The same seed again, on a second Gaussian stream attached to a second uniform stream. */
    G(closure, g2);                             /* the default constructor closure: no stream attached */
    G(attach, g2, u2);
    G(u_seed, u2, 2024);
    for (int i = 0; i < 64; i++) {
        float x = call_f(c, m_float, (RtArg[]){ RT_ARG_U(g2), RT_ARG_F(3.0f), RT_ARG_F(2.0f) }, 3);
        if (fbits(x) != fbits(first[i])) FAILF("the same seed on a second stream: draw %d is %.9g, the first stream gave %.9g", i, x, first[i]);
    }
    /* A copy continues where the original is (64 draws: an even number, no value held back). */
    G(copy_ctor, g3, g2);
    float a = call_f(c, m_float, (RtArg[]){ RT_ARG_U(g2), RT_ARG_F(0.0f), RT_ARG_F(1.0f) }, 3);
    G(u_seed, u2, 2024);
    for (int i = 0; i < 64; i++) call_f(c, m_float, (RtArg[]){ RT_ARG_U(g2), RT_ARG_F(3.0f), RT_ARG_F(2.0f) }, 3);
    float b = call_f(c, m_float, (RtArg[]){ RT_ARG_U(g3), RT_ARG_F(0.0f), RT_ARG_F(1.0f) }, 3);
    EXPECT("draw 65 from the copy against draw 65 from the original (bits)", fbits(b), fbits(a));
    EXPECT("operator= returns the destination", G(assign, g3, g2), (uint64_t)(uintptr_t)g3);
    /* The global function draws from the installed uniform stream: reseeding repeats it. */
    G(g_seed, 555);
    float x1 = call_f(c, g_gauss, (RtArg[]){ RT_ARG_F(0.0f), RT_ARG_F(1.0f) }, 2), x2 = call_f(c, g_gauss, (RtArg[]){ RT_ARG_F(0.0f), RT_ARG_F(1.0f) }, 2);
    G(g_seed, 555);
    float y1 = call_f(c, g_gauss, (RtArg[]){ RT_ARG_F(0.0f), RT_ARG_F(1.0f) }, 2), y2 = call_f(c, g_gauss, (RtArg[]){ RT_ARG_F(0.0f), RT_ARG_F(1.0f) }, 2);
    EXPECT_TRUE("RandomGaussianFloat repeats after RandomSeed (two draws)", fbits(x1) == fbits(y1) && fbits(x2) == fbits(y2));
    EXPECT_TRUE("the two draws differ", fbits(x1) != fbits(x2));
    gate2_value(c, "RandomGaussianFloat(0, 1) after RandomSeed(555): the bits of the first two draws", (uint64_t)fbits(x1) << 32 | fbits(x2));
    G(dtor, g); G(dtor, g2); G(dtor, g3);
}

/* ---- 62. Coroutines ---------------------------------------------------------------------------- */
/* Source: the public vstdlib/coroutine.h: Coroutine_Create(function, parameter) gives a handle;
 * Coroutine_Continue(handle, name) runs the function until it yields (true) or returns
 * (false); inside it Coroutine_IsActive is true and Coroutine_GetCurrentlyActive is the handle.
 * The body is a native stub of the driver that yields twice. */

static struct { GUESTFN yield, is_active, current; int stage; uint8_t active_in; int32_t current_in; uint64_t param; } co;
static void co_body(Ctx *ctx, Cb *cb) {
    co.param = cb->a[0];
    co.active_in = BOOL8(GUEST_CALL(ctx, co.is_active));
    co.current_in = (int32_t)GUEST_CALL(ctx, co.current);
    co.stage = 1;
    gate2_progress("coroutine body: first Coroutine_YieldToMain");
    GUEST_CALL(ctx, co.yield);
    co.stage = 2;
    gate2_progress("coroutine body: second Coroutine_YieldToMain");
    GUEST_CALL(ctx, co.yield);
    co.stage = 3;
}

static void run_62(Call *c) {
    VS(create, "Coroutine_Create");
    VS(cont, "Coroutine_Continue");
    VS(yield, "Coroutine_YieldToMain");
    VS(is_active, "Coroutine_IsActive");
    VS(current, "Coroutine_GetCurrentlyActive");
    VS(release, "Coroutine_ReleaseThreadMemory");
    co.yield = yield; co.is_active = is_active; co.current = current;
    cbs[0] = (Cb){ .hook = co_body };
    EXPECT("Coroutine_IsActive() outside any coroutine", GB(is_active), 0);
    int32_t h = GI(create, cb_stub[0], 0x1234);
    EXPECT_TRUE("Coroutine_Create returns a handle", h != 0);
    EXPECT("the body has not run before the first Continue", co.stage, 0);
    gate2_progress("Coroutine_Continue (1)");
    EXPECT("Coroutine_Continue (1): the body yielded", GB(cont, S32(h), 0), 1);
    EXPECT("the body's stage after the first Continue", co.stage, 1);
    EXPECT("the body's parameter", co.param, 0x1234);
    EXPECT("Coroutine_IsActive() inside the body", co.active_in, 1);
    EXPECT("Coroutine_GetCurrentlyActive() inside the body", co.current_in, h);
    EXPECT("Coroutine_IsActive() back outside", GB(is_active), 0);
    gate2_progress("Coroutine_Continue (2)");
    EXPECT("Coroutine_Continue (2): the body yielded again", GB(cont, S32(h), 0), 1);
    EXPECT("the body's stage after the second Continue", co.stage, 2);
    gate2_progress("Coroutine_Continue (3)");
    EXPECT("Coroutine_Continue (3): the body returned", GB(cont, S32(h), 0), 0);
    EXPECT("the body's stage at the end", co.stage, 3);
    EXPECT("calls of the body", cbs[0].calls, 1);
    G(release);
}

/* ---- 63. KeyValuesSystem: many symbols, and its memory pool ------------------------------------ */
/* Source: invariants.  3,000 distinct names get 3,000 distinct symbols, each symbol gives its
 * name back, and asking again gives the same symbol (the table grows several times on the
 * way).  Blocks from AllocKeyValuesMemory do not overlap and keep what is written to them. */

static void run_63(Call *c) {
    enum { N = 3000, BLOCKS = 200, SIZE = 48 };
    VS(get, "KeyValuesSystem");
    uint64_t kv = G(get);
    static int32_t sym[N];
    char *name = gate2_mem(32);
    for (int i = 0; i < N; i++) {
        snprintf(name, 32, "gate2_wide_key_%d_%x", i, (unsigned)i * 2654435761u);
        sym[i] = GI(slot(kv, 3), kv, name, 1);
        if (sym[i] < 0) FAILF("GetSymbolForString(name %d, create) = %d", i, sym[i]);
    }
    for (int i = 0; i < N; i++) {
        snprintf(name, 32, "gate2_wide_key_%d_%x", i, (unsigned)i * 2654435761u);
        uint64_t back = G(slot(kv, 4), kv, S32(sym[i]));
        if (!back || strcmp(gstr(back), name) != 0) FAILF("GetStringForSymbol(symbol of name %d) = \"%.40s\", not \"%s\"", i, back ? gstr(back) : "NULL", name);
        EXPECT("GetSymbolForString a second time, without create", GI(slot(kv, 3), kv, name, 0), sym[i]);
        if (i && sym[i] == sym[i - 1]) FAILF("names %d and %d share symbol %d", i - 1, i, sym[i]);
    }
    static int32_t sorted[N];
    memcpy(sorted, sym, sizeof sym);
    for (int i = 1; i < N; i++) { int32_t v = sorted[i]; int k = i; while (k && sorted[k - 1] > v) { sorted[k] = sorted[k - 1]; k--; } sorted[k] = v; }
    for (int i = 1; i < N; i++) if (sorted[i] == sorted[i - 1]) FAILF("two of the 3,000 names share symbol %d", sorted[i]);
    G(slot(kv, 0), kv, SIZE);                   /* RegisterSizeofKeyValues */
    static uint8_t *block[BLOCKS];
    for (int i = 0; i < BLOCKS; i++) {
        block[i] = (uint8_t *)(uintptr_t)G(slot(kv, 1), kv, SIZE);             /* AllocKeyValuesMemory */
        if (!block[i]) FAILF("AllocKeyValuesMemory(%d), block %d: NULL", SIZE, i);
        memset(block[i], i + 1, SIZE);
    }
    for (int i = 0; i < BLOCKS; i++)
        for (int k = 0; k < SIZE; k++) if (block[i][k] != (uint8_t)(i + 1)) FAILF("block %d of AllocKeyValuesMemory was overwritten at byte %d", i, k);
    for (int i = 0; i < BLOCKS; i++) G(slot(kv, 2), kv, block[i]);              /* FreeKeyValuesMemory */
    uint8_t *again = (uint8_t *)(uintptr_t)G(slot(kv, 1), kv, SIZE);
    EXPECT_TRUE("AllocKeyValuesMemory after everything was freed", again != NULL);
    G(slot(kv, 2), kv, again);
    snprintf(name, 32, "gate2_wide_key_%d_%x", 17, 17u * 2654435761u);
    EXPECT("the symbols survive the pool's use", GI(slot(kv, 3), kv, name, 0), sym[17]);
}

/* ---- 64. CreateNewThreadPool, DestroyThreadPool, g_pThreadPool ------------------------------- */
/* Source: finding 21, section 6.2 (the class's vtable is at vstdlib+0x5bcc8) and the invariant
 * that two pools are two objects.  No thread is started: a pool starts none until told to.
 * The entry first expected the pool the data export points to to be of the same class; its
 * vptr is vstdlib+0x5bd98, a class of its own, so only "an object of vstdlib's" is expected
 * of it now and the value is noted. */

static void run_64(Call *c) {
    VS(create, "CreateNewThreadPool");
    VS(destroy, "DestroyThreadPool");
    VS(global, "g_pThreadPool");
    unsigned threads = rt_thread_count();
    uint64_t a = G(create), b = G(create);
    EXPECT_TRUE("CreateNewThreadPool returns an object", a != 0 && b != 0);
    EXPECT_TRUE("two calls give two objects", a != b);
    EXPECT("the first pool's vptr - base", *(uint64_t *)(uintptr_t)a - c->vstdlib->base, 0x5bcc8);
    EXPECT("the second pool's vptr - base", *(uint64_t *)(uintptr_t)b - c->vstdlib->base, 0x5bcc8);
    uint64_t g = *(uint64_t *)(uintptr_t)global;
    EXPECT_TRUE("g_pThreadPool points to an object that is neither", g != 0 && g != a && g != b);
    uint64_t gv = *(uint64_t *)(uintptr_t)g - c->vstdlib->base;
    EXPECT_TRUE("the global pool's vptr lies in vstdlib", gv > 0x1000 && gv < 0x88000);
    EXPECT("guest threads after creating two pools", rt_thread_count(), threads);
    G(destroy, a);
    G(destroy, b);
    EXPECT("guest threads after destroying them", rt_thread_count(), threads);
    static char note[100];
    snprintf(note, sizeof note, "the global pool's vptr is vstdlib+%#llx, not the class CreateNewThreadPool makes", (unsigned long long)gv);
    gate2_note(c, note);
}

/* ---- 65. CCommandLine: argc/argv, CheckParm, the float ParmValue ---------------------------- */
/* Source: the public tier0/icommandline.h meanings with finding 21's slot numbers: slot 0
 * builds the line from argv (the entry first expected the arguments joined by spaces; the code
 * puts each in double quotes with a space after it, and the parameters read back without the
 * quotes), slot 4 returns the parameter and stores a
 * pointer to its value, slot 7 converts the value to a float (the C library's atof; the
 * values here are exact in binary) or returns the default. */

static void run_65(Call *c) {
    static const char *const args[] = { "game.exe", "-w", "640", "+map", "mp_box", "-f", "2.75", "-neg", "-0.5", "-e", "1.5e3" };
    enum { ARGC = sizeof args / sizeof args[0] };
    T0(command_line, "CommandLine");
    uint64_t cl = G(command_line);
    uint64_t *argv = gate2_mem(8 * (ARGC + 1));
    for (int i = 0; i < ARGC; i++) argv[i] = (uint64_t)(uintptr_t)gdup(args[i]);
    G(slot(cl, 0), cl, ARGC, argv);
    EXPECT_STR("GetCmdLine() after CreateCmdLine(argc, argv)", G(slot(cl, 3), cl),
               "\"game.exe\" \"-w\" \"640\" \"+map\" \"mp_box\" \"-f\" \"2.75\" \"-neg\" \"-0.5\" \"-e\" \"1.5e3\" ");
    EXPECT("ParmCount()", GI(slot(cl, 10), cl), ARGC);
    for (int i = 0; i < ARGC; i++) EXPECT_STR("GetParm(i)", G(slot(cl, 12), cl, i), args[i]);
    uint64_t *value = gate2_mem(8);
    *value = 0xEEEE;
    EXPECT_STR("CheckParm(-w, &value)", G(slot(cl, 4), cl, gdup("-w"), value), "-w");
    EXPECT_STR("CheckParm: the value", *value, "640");
    EXPECT("CheckParm(-nope, &value)", G(slot(cl, 4), cl, gdup("-nope"), value), 0);
    EXPECT_STR("CheckParm(+map, NULL)", G(slot(cl, 4), cl, gdup("+map"), 0), "+map");
    float f = call_f(c, slot(cl, 7), (RtArg[]){ RT_ARG_U(cl), RT_ARG_U(gdup("-f")), RT_ARG_F(9.0f) }, 3);
    EXPECT("ParmValue(-f, 9.0f) (bits of 2.75f)", fbits(f), fbits(2.75f));
    f = call_f(c, slot(cl, 7), (RtArg[]){ RT_ARG_U(cl), RT_ARG_U(gdup("-nope")), RT_ARG_F(9.5f) }, 3);
    EXPECT("ParmValue(-nope, 9.5f) (bits of the default)", fbits(f), fbits(9.5f));
    f = call_f(c, slot(cl, 7), (RtArg[]){ RT_ARG_U(cl), RT_ARG_U(gdup("-e")), RT_ARG_F(9.0f) }, 3);
    EXPECT("ParmValue(-e, 9.0f) (bits of 1500.0f)", fbits(f), fbits(1500.0f));
    f = call_f(c, slot(cl, 7), (RtArg[]){ RT_ARG_U(cl), RT_ARG_U(gdup("-w")), RT_ARG_F(9.0f) }, 3);
    EXPECT("ParmValue(-w, 9.0f) (bits of 640.0f)", fbits(f), fbits(640.0f));
    EXPECT("ParmValue(-w, 7) as an int", GI(slot(cl, 8), cl, gdup("-w"), 7), 640);
}

/* ---- 66. The game allocator under load -------------------------------------------------------- */
/* Source: invariants of any allocator.  Blocks do not overlap (each keeps its own pattern
 * while 3,000 are live), a block is at least as large as asked, Realloc keeps the common
 * prefix, and freeing in an order unrelated to allocation breaks nothing. */

static void run_66(Call *c) {
    enum { N = 3000 };
    T0(create, "CreateGlobalMemAlloc");
    uint64_t al = G(create);
    static uint8_t *p[N];
    static uint32_t size[N];
    uint32_t seed = 0x9E3779B9u;
#define NEXT() (seed = seed * 1664525u + 1013904223u, seed >> 8)
#define PAT(i, k) ((uint8_t)((i) * 31 + (k) * 7 + 3))
    for (int i = 0; i < N; i++) {
        uint32_t r = NEXT() % 100;
        size[i] = r < 60 ? 1 + NEXT() % 256 : r < 90 ? 257 + NEXT() % 4000 : r < 99 ? 4300 + NEXT() % 60000 : 70000 + NEXT() % 400000;
        p[i] = (uint8_t *)(uintptr_t)G(slot(al, 1), al, size[i]);
        if (!p[i]) FAILF("Alloc(%u), block %d: NULL", size[i], i);
        uint64_t got = G(slot(al, 8), al, p[i]);
        if (got < size[i]) FAILF("GetSize of block %d is %llu, less than the %u asked", i, (unsigned long long)got, size[i]);
        for (uint32_t k = 0; k < size[i]; k++) p[i][k] = PAT(i, k);
    }
    for (int i = 0; i < N; i++)
        for (uint32_t k = 0; k < size[i]; k++) if (p[i][k] != PAT(i, k)) FAILF("block %d (%u bytes) was overwritten at byte %u while all 3,000 were live", i, size[i], k);
    for (int i = 0; i < N; i += 2) { G(slot(al, 5), al, p[i]); p[i] = NULL; }
    for (int i = 1; i < N; i += 2) {
        uint32_t old = size[i], now = 1 + NEXT() % (old * 2 + 64);
        uint8_t *q = (uint8_t *)(uintptr_t)G(slot(al, 3), al, p[i], now);
        if (!q) FAILF("Realloc of block %d from %u to %u: NULL", i, old, now);
        uint32_t keep = old < now ? old : now;
        for (uint32_t k = 0; k < keep; k++) if (q[k] != PAT(i, k)) FAILF("Realloc of block %d from %u to %u changed byte %u", i, old, now, k);
        for (uint32_t k = keep; k < now; k++) q[k] = PAT(i, k);
        p[i] = q; size[i] = now;
    }
    for (int i = 0; i < N; i += 2) {
        size[i] = 1 + NEXT() % 1024;
        p[i] = (uint8_t *)(uintptr_t)G(slot(al, 1), al, size[i]);
        if (!p[i]) FAILF("Alloc(%u) into a freed place, block %d: NULL", size[i], i);
        for (uint32_t k = 0; k < size[i]; k++) p[i][k] = PAT(i, k);
    }
    for (int i = 0; i < N; i++)
        for (uint32_t k = 0; k < size[i]; k++) if (p[i][k] != PAT(i, k)) FAILF("block %d (%u bytes) was overwritten at byte %u after the reallocations", i, size[i], k);
    for (int i = 0; i < N; i++) G(slot(al, 5), al, p[(i * 1237) % N]);        /* 1237 is prime to 3000 */
#undef NEXT
#undef PAT
    uint64_t last = G(slot(al, 1), al, 64);
    EXPECT_TRUE("Alloc(64) after everything was freed", last != 0);
    G(slot(al, 5), al, last);
}

/* ---- 67. The allocator's entries with a file and a line ----------------------------------------- */
/* Source: invariants.  The mangled names give the types of the two CAlignedNewDelete<8>
 * operators with (int, const char *, int); slots 0, 2 and 4 of the allocator are, by finding
 * 21, the twins of Alloc, Realloc and Free that take a file name and a line.  Whatever they
 * do with the extra arguments, the memory must behave as memory. */

static void run_67(Call *c) {
    T0(create, "CreateGlobalMemAlloc");
    T0(op_new, "??2?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@SAPEAX_KHPEBDH@Z");
    T0(op_delete, "??3?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@SAXPEAXHPEBDH@Z");
    T0(plain_delete, "??3?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@SAXPEAX@Z");
    T0(assign, "??4?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@QEAAAEAV0@AEBV0@@Z");
    uint64_t al = G(create);
    char *file = gdup("gate2/calls_wide.c");
    uint8_t *p = (uint8_t *)(uintptr_t)G(slot(al, 0), al, 300, file, 67);
    EXPECT_TRUE("Alloc(300, file, line)", p != NULL);
    for (int i = 0; i < 300; i++) p[i] = (uint8_t)(i * 5 + 1);
    uint8_t *q = (uint8_t *)(uintptr_t)G(slot(al, 2), al, p, 5000, file, 68);
    EXPECT_TRUE("Realloc(p, 5000, file, line)", q != NULL);
    for (int i = 0; i < 300; i++) EXPECT("a byte of the first 300 after Realloc with file and line", q[i], (uint8_t)(i * 5 + 1));
    EXPECT_TRUE("GetSize after it", G(slot(al, 8), al, q) >= 5000);
    memset(q, 0x3C, 5000);
    G(slot(al, 4), al, q, file, 69);
    uint64_t blocks[16];
    for (int i = 0; i < 16; i++) {
        blocks[i] = G(op_new, 24 + i * 40, 0, file, 70);
        EXPECT_TRUE("operator new(size, int, file, line)", blocks[i] != 0);
        EXPECT("operator new(size, int, file, line): the pointer modulo 8", blocks[i] & 7, 0);
        memset((void *)(uintptr_t)blocks[i], 0x40 + i, (size_t)(24 + i * 40));
    }
    for (int i = 0; i < 16; i++) {
        const uint8_t *b = (const uint8_t *)(uintptr_t)blocks[i];
        for (int k = 0; k < 24 + i * 40; k++) if (b[k] != 0x40 + i) FAILF("block %d from operator new was overwritten at byte %d", i, k);
    }
    for (int i = 0; i < 16; i++) { if (i & 1) G(op_delete, blocks[i], 0, file, 71); else G(plain_delete, blocks[i]); }
    uint8_t *x = gfill(8, 0xEE), *y = gfill(8, 0x11);
    EXPECT("operator= of the empty class returns the destination", G(assign, x, y), (uint64_t)(uintptr_t)x);
}

/* ---- 68. Plat_GetModuleFilename, Plat_IsInDebugSession ---------------------------------------- */
/* Source: Windows, through the runtime's own exports: the export must give what
 * GetModuleFileNameA(NULL, ...) and IsDebuggerPresent give when called directly. */

static void run_68(Call *c) {
    T0(module_name, "Plat_GetModuleFilename");
    T0(in_debug, "Plat_IsInDebugSession");
    RtModule *k32 = rt_module_find("kernel32.dll");
    EXPORT(direct_name, k32, "GetModuleFileNameA");
    EXPORT(direct_debug, k32, "IsDebuggerPresent");
    char *a = gfill(300, 0xEE), *b = gfill(300, 0xEE);
    G(module_name, a, 260);
    uint32_t n = (uint32_t)G(direct_name, 0, b, 260);
    EXPECT_TRUE("GetModuleFileNameA(NULL) gives a name", n > 0 && n < 260);
    if (strcmp(a, b) != 0) FAILF("Plat_GetModuleFilename gives \"%.100s\", GetModuleFileNameA(NULL) \"%.100s\"", a, b);
    EXPECT("the byte after the 260 given", (uint8_t)a[260], 0xEE);
    EXPECT("Plat_IsInDebugSession() against IsDebuggerPresent()", GB(in_debug), G(direct_debug) != 0);
}

/* ---- 69. GetCurrentDate, GetCurrentDayOfTheWeek, GetCurrentDayOfTheYear, Plat_GetTimeString -- */
/* Source: the host's calendar in the zone the runtime reports (a fixed offset, as rank 25),
 * read before and after each call so that midnight cannot fail the entry; the instructions
 * give the argument order (day, month, year) and the bases (month 1 to 12, the full year).
 * Plat_GetTimeString(tm, out, size) is the C library's asctime of the tm, cut to the size. */

static void run_69(Call *c) {
    T0(date, "GetCurrentDate");
    T0(week_day, "GetCurrentDayOfTheWeek");
    T0(year_day, "GetCurrentDayOfTheYear");
    T0(time_string, "Plat_GetTimeString");
    int64_t off = -(int64_t)rt_time_zone_bias_minutes() * 60;
    int32_t *o = gfill(32, 0xEE);
    struct tm h0, h1;
    time_t t0 = time(NULL) + (time_t)off;
    gmtime_r(&t0, &h0);
    G(date, &o[0], &o[1], &o[2]);
    G(week_day, &o[3]);
    G(year_day, &o[4]);
    time_t t1 = time(NULL) + (time_t)off;
    gmtime_r(&t1, &h1);
#define EITHER(what, got, f) do { if ((got) != h0.f && (got) != h1.f) FAILF("%s is %d; the host's calendar in the zone UTC%+lld min says %d", what, (got), (long long)(off / 60), h1.f); } while (0)
    EITHER("GetCurrentDate: the day", o[0], tm_mday);
    EITHER("GetCurrentDate: the month, less 1", o[1] - 1, tm_mon);
    EITHER("GetCurrentDate: the year, less 1900", o[2] - 1900, tm_year);
    EITHER("GetCurrentDayOfTheWeek", o[3], tm_wday);
    EITHER("GetCurrentDayOfTheYear", o[4], tm_yday);
#undef EITHER
    EXPECT("the int after the five written", (uint32_t)o[5], 0xEEEEEEEEu);
    time_t fixed = 1700000000;
    struct tm tm;
    gmtime_r(&fixed, &tm);
    int32_t *gtm = gate2_mem(36);               /* the nine ints of the Windows struct tm */
    const int fields[9] = { tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_wday, tm.tm_yday, 0 };
    for (int i = 0; i < 9; i++) gtm[i] = fields[i];
    char want[32], *out = gfill(64, 0xEE);
    asctime_r(&tm, want);                       /* "Tue Nov 14 22:13:20 2023\n" */
    G(time_string, gtm, out, 64);
    if (strncmp(out, want, strlen(want)) != 0 || strlen(out) != strlen(want)) FAILF("Plat_GetTimeString gives \"%.40s\", asctime \"%.40s\"", out, want);
    memset(out, 0xEE, 64);
    G(time_string, gtm, out, 11);
    EXPECT_TRUE("Plat_GetTimeString into 11 bytes: the first 10 characters and a terminator", memcmp(out, want, 10) == 0 && out[10] == 0);
    EXPECT("Plat_GetTimeString into 11 bytes: the byte after", (uint8_t)out[11], 0xEE);
}

/* ---- 70. Statistics descriptors and CCallStackStorage ---------------------------------------- */
/* Source: the export table (a constructor must install the exported vtable of its class), the
 * invariant that a copy equals its original, and for CCallStackStorage the public header's
 * meaning: the constructor calls the capture function it is given with the object's frame
 * array, its capacity and the number of frames to skip, and keeps the count it returns. */

static void capture_hook(Ctx *ctx, Cb *cb) {
    (void)ctx;
    uint64_t *frames = (uint64_t *)(uintptr_t)cb->a[0];
    if ((int32_t)cb->a[1] >= 3) { frames[0] = 0x1111; frames[1] = 0x2222; frames[2] = 0x3333; }
}

static void run_70(Call *c) {
    T0(field_ctor, "??0BasicStatStructFieldDesc@@QEAA@W4BasicStatStructFieldTypes_t@@W4BasicStatStructFieldCombineMethods_t@@@Z");
    T0(field_copy, "??0BasicStatStructFieldDesc@@QEAA@AEBV0@@Z");
    T0(field_assign, "??4BasicStatStructFieldDesc@@QEAAAEAV0@AEBV0@@Z");
    T0(field_vt, "??_7BasicStatStructFieldDesc@@6B@");
    T0(funcs_ctor, "??0CallStackStatStructDescFuncs@@QEAA@XZ");
    T0(funcs_copy, "??0CallStackStatStructDescFuncs@@QEAA@AEBV0@@Z");
    T0(funcs_assign, "??4CallStackStatStructDescFuncs@@QEAAAEAV0@AEBV0@@Z");
    T0(funcs_vt, "??_7CallStackStatStructDescFuncs@@6B@");
    T0(storage_ctor, "??0CCallStackStorage@@QEAA@P6AHPEAPEAXHH@ZI@Z");
    T0(storage_copy, "??0CCallStackStorage@@QEAA@AEBV0@@Z");
    T0(storage_assign, "??4CCallStackStorage@@QEAAAEAV0@AEBV0@@Z");
    uint64_t *a = gfill(0x40, 0xEE), *b = gfill(0x40, 0xEE), *d = gfill(0x40, 0xEE);
    EXPECT("BasicStatStructFieldDesc(3, 2) returns the object", G(field_ctor, a, 3, 2), (uint64_t)(uintptr_t)a);
    EXPECT("its vptr against the exported vtable", a[0], field_vt);
    /* The object is 0x28 bytes: the vptr, 0x18 bytes of a base class this constructor leaves
     * alone, and the two enumerators at +0x20 and +0x24 (read from its 6 instructions after
     * the entry first looked for them right after the vptr). */
    const uint32_t *f = (const uint32_t *)&a[4];
    EXPECT_TRUE("the two enumerators are stored at +0x20 and +0x24", f[0] == 3 && f[1] == 2);
    EXPECT_TRUE("the constructor writes nothing else", a[1] == 0xEEEEEEEEEEEEEEEEull && a[3] == 0xEEEEEEEEEEEEEEEEull && a[5] == 0xEEEEEEEEEEEEEEEEull);
    a[1] = 0x0101; a[2] = 0x0202; a[3] = 0x0303;
    G(field_copy, b, a);
    EXPECT_TRUE("the copy constructor copies the 0x28 bytes", memcmp(a, b, 0x28) == 0 && b[5] == 0xEEEEEEEEEEEEEEEEull);
    G(field_ctor, d, 1, 1);
    EXPECT("operator= returns the destination", G(field_assign, d, a), (uint64_t)(uintptr_t)d);
    EXPECT_TRUE("operator= copies the fields and keeps the vptr", memcmp(d, a, 0x28) == 0 && d[5] == 0xEEEEEEEEEEEEEEEEull);
    memset(a, 0xEE, 0x40); memset(b, 0xEE, 0x40);
    G(funcs_ctor, a);
    EXPECT("CallStackStatStructDescFuncs: the vptr against the exported vtable", a[0], funcs_vt);
    G(funcs_copy, b, a);
    EXPECT("its copy constructor: the vptr", b[0], funcs_vt);
    EXPECT("its operator= returns the destination", G(funcs_assign, b, a), (uint64_t)(uintptr_t)b);
    /* CCallStackStorage with a capture function of the driver's that reports three frames. */
    uint64_t *s1 = gfill(0x1000, 0xEE), *s2 = gfill(0x1000, 0xEE);
    cbs[0] = (Cb){ .hook = capture_hook, .ret = 3 };
    G(storage_ctor, s1, cb_stub[0], 5);
    EXPECT("calls of the capture function", cbs[0].calls, 1);
    uint64_t at = cbs[0].a[0] - (uint64_t)(uintptr_t)s1;
    int32_t cap = (int32_t)cbs[0].a[1], skip = (int32_t)cbs[0].a[2];
    if (at > 64 || cap < 3 || cap > 450 || skip < 5 || skip > 7) FAILF("the capture function was given the object + %llu, capacity %d, skip %d", (unsigned long long)at, cap, skip);
    const uint64_t *fr = (const uint64_t *)((const uint8_t *)s1 + at);
    EXPECT_TRUE("the three frames are in the object", fr[0] == 0x1111 && fr[1] == 0x2222 && fr[2] == 0x3333);
    EXPECT("the count kept after the frame array", *(const uint32_t *)&fr[cap], 3);
    G(storage_copy, s2, s1);
    EXPECT_TRUE("the copy constructor copies the three frames and the count", memcmp((const uint8_t *)s2 + at, fr, 24) == 0 && *(const uint32_t *)((const uint8_t *)s2 + at + 8 * (size_t)cap) == 3);
    memset(s2, 0, 0x1000);
    EXPECT("operator= returns the destination", G(storage_assign, s2, s1), (uint64_t)(uintptr_t)s2);
    EXPECT_TRUE("operator= copies the three frames and the count", memcmp((const uint8_t *)s2 + at, fr, 24) == 0 && *(const uint32_t *)((const uint8_t *)s2 + at + 8 * (size_t)cap) == 3);
    EXPECT("calls of the capture function at the end", cbs[0].calls, 1);
}

/* ---- 71. Plat_gmtime and Plat_ConvertToLocalTime over the whole range ------------------------- */
/* Source: the host's gmtime_r.  20,000 times spread from 1970 to the end of year 3000, and
 * every hour around two leap days and a century that is not a leap year; local time is UTC
 * shifted by the runtime's fixed zone offset (rank 25). */

static bool tm_same(Call *c, const char *what, int64_t t, const int32_t *out, const struct tm *h) {
    const int want[9] = { h->tm_sec, h->tm_min, h->tm_hour, h->tm_mday, h->tm_mon, h->tm_year, h->tm_wday, h->tm_yday, 0 };
    for (unsigned k = 0; k < 9; k++)
        if (out[k] != want[k]) { gate2_fail_text(c, "%s(%lld): field %u of struct tm is %d, the host gives %d", what, (long long)t, k, out[k], want[k]); return false; }
    return true;
}

static void run_71(Call *c) {
    static const int64_t around[] = { 951782400 /* 2000-02-29 */, 4107542400 /* 2100-03-01 */, 13574563200 /* 2400-02-29 */, 68169600 /* 1972-02-29 */ };
    T0(gm, "Plat_gmtime");
    T0(local, "Plat_ConvertToLocalTime");
    int64_t off = -(int64_t)rt_time_zone_bias_minutes() * 60;
    int32_t *out = gate2_mem(64);
    uint64_t seed = 0x243F6A8885A308D3ull;
    struct tm h;
    for (int i = 0; i < 20000; i++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        int64_t t = (int64_t)((seed >> 11) % 32535216000ull);
        time_t tt = (time_t)t;
        gmtime_r(&tt, &h);
        EXPECT("Plat_gmtime returns 0 inside the range", GI(gm, (uint64_t)t, out), 0);
        if (!tm_same(c, "Plat_gmtime", t, out, &h)) return;
        if (i < 3000 && t > 86400 && t < 32535100000) {
            tt = (time_t)(t + off);
            gmtime_r(&tt, &h);
            G(local, (uint64_t)t, out);
            if (!tm_same(c, "Plat_ConvertToLocalTime", t, out, &h)) return;
        }
    }
    for (unsigned a = 0; a < sizeof around / sizeof around[0]; a++)
        for (int hour = -48; hour <= 48; hour++) {
            int64_t t = around[a] + hour * 3600 + 59;
            time_t tt = (time_t)t;
            gmtime_r(&tt, &h);
            EXPECT("Plat_gmtime returns 0 around a leap day", GI(gm, (uint64_t)t, out), 0);
            if (!tm_same(c, "Plat_gmtime", t, out, &h)) return;
        }
}

/* ---- 72. EncodeBinaryToString / DecodeBinaryFromString, 400 random buffers ------------------ */
/* Source: the invariant that decoding an encoding gives the bytes back, with rank 3's
 * (finding 21's) length rule and its "no output byte is below 0x80". */

static void run_72(Call *c) {
    T0(enc, "EncodeBinaryToString");
    T0(dec, "DecodeBinaryFromString");
    uint8_t *src = gate2_mem(2100), *str = gate2_mem(4300), *out = gate2_mem(2100);
    uint64_t *end = gate2_mem(8);
    uint32_t seed = 0xC0FFEE11u;
    for (int i = 0; i < 400; i++) {
        seed = seed * 1664525u + 1013904223u;
        int n = (int)((seed >> 12) % 2049), len = n + 4 + (n + 6) / 7;
        int kind = i % 4;                       /* random bytes, all 0xFF, all 0, all 0x80 */
        for (int k = 0; k < n; k++) { seed = seed * 1664525u + 1013904223u; src[k] = kind == 0 ? (uint8_t)(seed >> 24) : kind == 1 ? 0xFF : kind == 2 ? 0 : 0x80; }
        memset(str, 0xEE, 4300);
        EXPECT("EncodeBinaryToString: length", GI(enc, src, S32(n), str, S32(len + 1)), len);
        for (int k = 0; k < len; k++) if (str[k] < 0x80) FAILF("buffer %d (%d bytes): output byte %d is %#x", i, n, k, str[k]);
        EXPECT("EncodeBinaryToString: the terminator", str[len], 0);
        EXPECT("EncodeBinaryToString: the byte after the terminator", str[len + 1], 0xEE);
        memset(out, 0xEE, 2100);
        EXPECT("DecodeBinaryFromString: length", GI(dec, str, out, S32(n), end), n);
        if (memcmp(out, src, (size_t)n) != 0) FAILF("buffer %d (%d bytes, kind %d): the decoded bytes differ from the encoded ones", i, n, kind);
        EXPECT("DecodeBinaryFromString: the byte after the output", out[n], 0xEE);
    }
}

/* ---- 73. UTF-8 and UTF-16, 300 random strings --------------------------------------------------- */
/* Source: an independent encoder in the driver (the Unicode standard's UTF-8 and UTF-16
 * forms) for random code points of every length class, surrogates excluded; counts as rank
 * 14 found them (elements or bytes written, terminator included). */

static void run_73(Call *c) {
    VS(u8_to_uni, "V_UTF8ToUnicode");
    VS(uni_to_u8, "V_UnicodeToUTF8");
    VS(u8_to_ucs2, "V_UTF8ToUCS2");
    VS(ucs2_to_u8, "V_UCS2ToUTF8");
    enum { MAXCP = 40 };
    uint8_t *s8 = gate2_mem(MAXCP * 4 + 8), *d8 = gate2_mem(MAXCP * 4 + 16);
    uint16_t *s16 = gate2_mem(MAXCP * 4 + 8), *d16 = gate2_mem(MAXCP * 4 + 16);
    uint32_t seed = 0xDEADBEEFu;
    for (int i = 0; i < 300; i++) {
        int n8 = 0, n16 = 0, ncp;
        seed = seed * 1664525u + 1013904223u;
        ncp = (int)((seed >> 10) % (MAXCP + 1));
        for (int k = 0; k < ncp; k++) {
            seed = seed * 1664525u + 1013904223u;
            uint32_t r = seed >> 8, cls = r & 3, cp;
            cp = cls == 0 ? 1 + (r >> 2) % 0x7F : cls == 1 ? 0x80 + (r >> 2) % 0x780 : cls == 2 ? 0x800 + (r >> 2) % 0xF800 : 0x10000 + (r >> 2) % 0x100000;
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0x4E2D;
            if (cp < 0x80) s8[n8++] = (uint8_t)cp;
            else if (cp < 0x800) { s8[n8++] = (uint8_t)(0xC0 | cp >> 6); s8[n8++] = (uint8_t)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { s8[n8++] = (uint8_t)(0xE0 | cp >> 12); s8[n8++] = (uint8_t)(0x80 | (cp >> 6 & 0x3F)); s8[n8++] = (uint8_t)(0x80 | (cp & 0x3F)); }
            else { s8[n8++] = (uint8_t)(0xF0 | cp >> 18); s8[n8++] = (uint8_t)(0x80 | (cp >> 12 & 0x3F)); s8[n8++] = (uint8_t)(0x80 | (cp >> 6 & 0x3F)); s8[n8++] = (uint8_t)(0x80 | (cp & 0x3F)); }
            if (cp < 0x10000) s16[n16++] = (uint16_t)cp;
            else { s16[n16++] = (uint16_t)(0xD800 + ((cp - 0x10000) >> 10)); s16[n16++] = (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)); }
        }
        s8[n8++] = 0; s16[n16++] = 0;           /* counts with the terminator from here on */
        memset(d16, 0xEE, MAXCP * 4 + 16);
        EXPECT("V_UTF8ToUnicode: count", GI(u8_to_uni, s8, d16, S32(MAXCP * 4 + 8)), n16);
        if (memcmp(d16, s16, (size_t)n16 * 2) != 0) FAILF("string %d (%d code points): V_UTF8ToUnicode's UTF-16 differs from the driver's", i, ncp);
        EXPECT("V_UTF8ToUnicode: the element after the terminator", d16[n16], 0xEEEE);
        memset(d8, 0xEE, MAXCP * 4 + 16);
        EXPECT("V_UnicodeToUTF8: count", GI(uni_to_u8, s16, d8, S32(MAXCP * 4 + 8)), n8);
        if (memcmp(d8, s8, (size_t)n8) != 0) FAILF("string %d (%d code points): V_UnicodeToUTF8's UTF-8 differs from the driver's", i, ncp);
        EXPECT("V_UnicodeToUTF8: the byte after the terminator", d8[n8], 0xEE);
        memset(d16, 0xEE, MAXCP * 4 + 16);
        EXPECT("V_UTF8ToUCS2: count", GI(u8_to_ucs2, s8, S32(n8), d16, S32(MAXCP * 4 + 8)), n16);
        if (memcmp(d16, s16, (size_t)n16 * 2) != 0) FAILF("string %d (%d code points): V_UTF8ToUCS2's UTF-16 differs from the driver's", i, ncp);
        memset(d8, 0xEE, MAXCP * 4 + 16);
        EXPECT("V_UCS2ToUTF8: count", GI(ucs2_to_u8, s16, d8, S32(MAXCP * 4 + 8)), n8);
        if (memcmp(d8, s8, (size_t)n8) != 0) FAILF("string %d (%d code points): V_UCS2ToUTF8's UTF-8 differs from the driver's", i, ncp);
    }
}

/* ---- 74. Events and semaphores across threads ------------------------------------------------- */
/* Source: Windows.  A thread waiting on an event wakes when another thread sets it, and not
 * before; ReleaseSemaphore fails, and changes nothing, when the count would pass the maximum. */

typedef struct { GUESTFN wait; void *ev; uint8_t got; double ms; } WaitJob;
static void wait_thread(Call *c, void *arg) {
    WaitJob *j = arg;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    j->got = GB(j->wait, j->ev, 20000);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    j->ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
}

static void run_74(Call *c) {
    T0(ev_ctor, "??0CThreadEvent@@QEAA@_N@Z");
    T0(ev_set, "?Set@CThreadEvent@@QEAA_NXZ");
    T0(ev_wait, "?Wait@CThreadEvent@@QEAA_NI@Z");
    T0(ev_dtor, "??1CThreadEvent@@QEAA@XZ");
    T0(sem_ctor, "??0CThreadSemaphore@@QEAA@HH@Z");
    T0(sem_release, "?Release@CThreadSemaphore@@QEAA_NHPEAH@Z");
    T0(sem_wait, "?Wait@CThreadSyncObject@@QEAA_NI@Z");
    T0(sem_dtor, "??1CThreadSemaphore@@QEAA@XZ");
    uint64_t *ev = gate2_mem(16), *sem = gate2_mem(16);
    int32_t *prev = gate2_mem(4);
    for (int manual = 0; manual < 2; manual++) {
        G(ev_ctor, ev, manual);
        WaitJob j = { ev_wait, ev, 0xEE, 0 };
        void *t = gate2_thread_start(c, wait_thread, &j);
        if (!t) return;
        usleep(40000);
        G(ev_set, ev);
        if (!gate2_thread_finish(c, t)) return;
        EXPECT("the waiting thread's Wait(20000)", j.got, 1);
        if (j.ms < 30.0 || j.ms > 15000.0) FAILF("the waiting thread's Wait returned after %.1f ms; the event was set after 40", j.ms);
        EXPECT("the event afterwards: Wait(0) (1 = still set, a manual-reset event)", GB(ev_wait, ev, 0), manual);
        G(ev_dtor, ev);
    }
    G(sem_ctor, sem, 2, 3);
    *prev = -1;
    EXPECT("semaphore(2, 3): Release(2), which would pass the maximum", GB(sem_release, sem, 2, prev), 0);
    EXPECT("the previous-count output of the refused Release is untouched", *prev, -1);
    EXPECT("Release(1, &prev)", GB(sem_release, sem, 1, prev), 1);
    EXPECT("prev", *prev, 2);
    for (int i = 0; i < 3; i++) EXPECT("Wait(0), three times", GB(sem_wait, sem, 0), 1);
    EXPECT("Wait(0) a fourth time", GB(sem_wait, sem, 0), 0);
    WaitJob j = { sem_wait, sem, 0xEE, 0 };
    void *t = gate2_thread_start(c, wait_thread, &j);
    if (!t) return;
    usleep(40000);
    G(sem_release, sem, 1, 0);
    if (!gate2_thread_finish(c, t)) return;
    EXPECT("a thread waiting on the semaphore wakes on Release", j.got, 1);
    if (j.ms < 30.0) FAILF("the thread waiting on the semaphore returned after %.1f ms; the release came after 40", j.ms);
    G(sem_dtor, sem);
}

/* ---- 75. Coroutine bookkeeping that switches nothing ------------------------------------------ */
/* Source: the public vstdlib/coroutine.h and invariants: creating a coroutine does not run
 * it, two coroutines have two handles, and outside any coroutine nothing is active.  Rank 62
 * is the one that switches (and stops at the C runtime's setjmp, which the lifter skips). */

static void run_75(Call *c) {
    VS(create, "Coroutine_Create");
    VS(is_active, "Coroutine_IsActive");
    VS(current, "Coroutine_GetCurrentlyActive");
    VS(validate, "Coroutine_ValidateGlobals");
    cbs[0] = (Cb){ 0 };
    int32_t outside = GI(current);
    int32_t a = GI(create, cb_stub[0], 1), b = GI(create, cb_stub[0], 2);
    EXPECT_TRUE("two coroutines have two handles, neither of them what is current outside", a != b && a != outside && b != outside);
    EXPECT("creating a coroutine does not run its body", cbs[0].calls, 0);
    EXPECT("Coroutine_IsActive() outside any coroutine", GB(is_active), 0);
    EXPECT("Coroutine_GetCurrentlyActive() is what it was", GI(current), outside);
    G(validate, 0);                             /* one instruction: ret */
}

/* ---- 76. The job system's FIFO lock ------------------------------------------------------------ */
/* Source: JT_InitFifoLock by its nine instructions (the qword at +0 and 64 words after it are
 * set to 0x1c, the thread index of "no job thread": 0x88 bytes of a larger object); for the rest invariants: an
 * uncontended acquire and release leave the lock as initialised, and two threads with
 * different job-thread indices exclude each other.  The job system is set up with no worker
 * threads first, as rank 28 does.  JT_GetCurrentJob and JT_ThreadIndexEnd are only required to
 * answer the same twice. */

typedef struct { GUESTFN set_idx; CountJob count; } FifoJob;
static void fifo_thread(Call *c, void *arg) {
    FifoJob *j = arg;
    G(j->set_idx, 3);
    count_loop(c, &j->count);
}

static void run_76(Call *c) {
    T0(command_line, "CommandLine");
    T0(configure, "ConfigureAndInitJobThreads");
    T0(init, "?JT_InitFifoLock@@YAXPEAUJobFifoLock_s@@@Z");
    T0(acquire, "?JT_AcquireFifoLockOrHelp@@YAXPEAUJobFifoLock_s@@@Z");
    T0(release, "?JT_ReleaseFifoLock@@YAXPEAUJobFifoLock_s@@@Z");
    T0(set_idx, "?JT_SetThreadIdx@@YAXI@Z");
    T0(cur_job, "?JT_GetCurrentJob@@YA?AW4JobID_t@@XZ");
    T0(index_end, "?JT_ThreadIndexEnd@@YAIXZ");
    uint64_t cl = G(command_line);
    G(slot(cl, 1), cl, gdup("game.exe -numworkerthreads 0"));
    G(configure);
    uint8_t *lock = gfill(0x200, 0xEE);
    static uint8_t fresh[0x88];
    G(init, lock);
    EXPECT("JT_InitFifoLock: the qword at +0", *(uint64_t *)lock, 0x1c);
    for (int i = 0; i < 64; i++) EXPECT("JT_InitFifoLock: one of the 64 words after it", *(uint16_t *)(lock + 8 + 2 * i), 0x1c);
    for (int i = 0x88; i < 0x200; i++) EXPECT("JT_InitFifoLock: a byte after the 0x88 it initialises", lock[i], 0xEE);
    memcpy(fresh, lock, 0x88);
    EXPECT("JT_GetCurrentJob() twice", G(cur_job), G(cur_job));
    EXPECT("JT_ThreadIndexEnd() twice", G(index_end), G(index_end));
    G(set_idx, 2);
    gate2_progress("JT_AcquireFifoLockOrHelp, uncontended");
    G(acquire, lock);
    EXPECT_TRUE("the lock's bytes change while it is held", memcmp(fresh, lock, 0x88) != 0);
    G(release, lock);
    EXPECT_TRUE("after the release the lock is as initialised", memcmp(fresh, lock, 0x88) == 0);
    for (int i = 0; i < 1000; i++) { G(acquire, lock); G(release, lock); }
    EXPECT_TRUE("after 1,000 more rounds the lock is as initialised", memcmp(fresh, lock, 0x88) == 0);
    gate2_progress("JT_AcquireFifoLockOrHelp from two threads");
    static volatile uint64_t counter;
    counter = 0;
    FifoJob j = { set_idx, { acquire, release, lock, &counter, 5000 } };
    void *t = gate2_thread_start(c, fifo_thread, &j);
    if (!t) return;
    count_loop(c, &j.count);
    if (!gate2_thread_finish(c, t)) return;
    EXPECT("two threads each added 1 to a counter 5,000 times under the FIFO lock: the sum", counter, 10000);
    /* Not "as initialised" here: the entry first expected that and found the head back at
     * 0x1c in its low byte only, with a count above it that differs from run to run, and the
     * words of the two threads' indices changed (what a queue of waiters leaves behind).  What must hold is that nobody holds or waits, and that the lock still works. */
    static char note[160];
    size_t at = (size_t)snprintf(note, sizeof note, "after contention the qword at +0 is %#llx; words that are not 0x1c:", (unsigned long long)*(uint64_t *)lock);
    for (int i = 0; i < 64; i++)
        if (*(uint16_t *)(lock + 8 + 2 * i) != 0x1c && at < sizeof note - 20) at += (size_t)snprintf(note + at, sizeof note - at, " [%d]=%#x", i, *(uint16_t *)(lock + 8 + 2 * i));
    gate2_note(c, note);
    EXPECT("after the two threads: the low byte of the qword at +0 (0x1c: nobody; the bytes above it count contended rounds)", *(uint8_t *)lock, 0x1c);
    for (int i = 0; i < 100; i++) { G(acquire, lock); G(release, lock); }
    /* The object is larger than the 0x88 bytes JT_InitFifoLock writes: the entry first guarded
     * everything after them and found the contended release storing job-thread indices at
     * +0x88 (one instruction of the original: a word store at lock + 0x88 + 2 * index).  With
     * 64 such words the object ends at +0x108. */
    for (int i = 0x108; i < 0x200; i++) if (lock[i] != 0xEE) FAILF("byte +%#x, after the 0x108 of the lock, is %#x at the end (%s)", i, lock[i], note);
}

/* ---- 77. V_UTF8_strncpy, 600 random cuts ---------------------------------------------------------- */
/* Source: an independent computation of rank 5's (finding 21's) rule: the copy is the longest
 * prefix of whole characters that fits in maxLen - 1 bytes, terminated; dst is returned. */

static void run_77(Call *c) {
    VS(f, "V_UTF8_strncpy");
    uint8_t *src = gate2_mem(200), *dst = gate2_mem(220);
    uint32_t seed = 0x1234ABCDu;
    for (int i = 0; i < 600; i++) {
        int n = 0, starts[64], nstarts = 0;
        seed = seed * 1664525u + 1013904223u;
        int ncp = (int)((seed >> 10) % 40);
        for (int k = 0; k < ncp; k++) {
            seed = seed * 1664525u + 1013904223u;
            int len = 1 + (int)((seed >> 9) & 3);
            starts[nstarts++] = n;
            if (len == 1) src[n++] = (uint8_t)('a' + (seed >> 16) % 26);
            else {
                src[n++] = (uint8_t)(len == 2 ? 0xC3 : len == 3 ? 0xE2 : 0xF0);
                if (len == 4) src[n++] = 0x9F;
                for (int m = len == 4 ? 2 : 1; m < len; m++) src[n++] = (uint8_t)(0x80 | ((seed >> (m * 5)) & 0x3F));
            }
        }
        starts[nstarts] = n;
        src[n] = 0;
        seed = seed * 1664525u + 1013904223u;
        int max = 1 + (int)((seed >> 8) % (unsigned)(n + 6)), want = 0;
        for (int k = 0; k <= nstarts; k++) if (starts[k] <= max - 1) want = starts[k];
        memset(dst, 0xEE, 220);
        EXPECT("V_UTF8_strncpy returns dst", G(f, dst, src, S32(max)), (uint64_t)(uintptr_t)dst);
        if ((int)strlen((char *)dst) != want || memcmp(dst, src, (size_t)want) != 0)
            FAILF("string %d (%d bytes, %d characters) with maxLen %d: copied %d bytes, the longest whole-character prefix has %d", i, n, ncp, max, (int)strlen((char *)dst), want);
        for (int k = max; k < 220; k++) if (dst[k] != 0xEE) FAILF("string %d with maxLen %d: byte %d of the destination was written", i, max, k);
    }
}

/* ---- 78. CThread while its thread runs --------------------------------------------------------- */
/* Source: what a thread class must say while its Run has not returned (alive, a handle, an
 * id; a Join with a short timeout fails), Windows thread priorities read back, and the Join
 * succeeding once Run is let go.  Run is the driver's stub and waits on an event. */

typedef struct { GUESTFN wait; void *ev; } RunWait;
static void run_wait_hook(Ctx *ctx, Cb *cb) {
    RunWait *w = cb->user;
    cb->ret = (uint64_t)(GUEST_CALL(ctx, w->wait, w->ev, 30000) & 0xff ? 77 : 78);
}

static void run_78(Call *c) {
    T0(ctor, "??0CThread@@QEAA@XZ");
    T0(vtable, "??_7CThread@@6B@");
    T0(run, "?Run@CThread@@MEAAHXZ");
    T0(start, "?Start@CThread@@UEAA_N_KW4ThreadPriorityEnum_t@1@@Z");
    T0(join, "?Join@CThread@@QEAA_NI@Z");
    T0(get_result, "?GetResult@CThread@@QEAAHXZ");
    T0(is_alive, "?IsAlive@CThread@@QEAA_NXZ");
    T0(get_id, "?GetThreadID@CThread@@IEBAIXZ");
    T0(get_handle, "?GetThreadHandle@CThread@@QEAAPEAUV_ThreadHandle_t__@@XZ");
    T0(get_pri, "?GetPriority@CThread@@QEBA?AW4ThreadPriority_e@@XZ");
    T0(set_pri, "?SetPriority@CThread@@QEAA_NW4ThreadPriority_e@@@Z");
    T0(ev_ctor, "??0CThreadEvent@@QEAA@_N@Z");
    T0(ev_set, "?Set@CThreadEvent@@QEAA_NXZ");
    T0(ev_wait, "?Wait@CThreadEvent@@QEAA_NI@Z");
    enum { SLOTS = 12 };
    uint64_t *t = gfill(0x400, 0xEE), *vt = gate2_mem(SLOTS * 8), *ev = gate2_mem(16);
    const uint64_t *orig = (const uint64_t *)(uintptr_t)vtable;
    int run_slot = -1;
    for (int i = 0; i < SLOTS; i++) { vt[i] = orig[i]; if (orig[i] == run && run_slot < 0) run_slot = i; }
    if (run_slot < 0) FAILF("the exported Run is not among the first %d slots of the exported vtable", SLOTS);
    G(ev_ctor, ev, 1);
    RunWait w = { ev_wait, ev };
    cbs[0] = (Cb){ .hook = run_wait_hook, .user = &w };
    vt[run_slot] = cb_stub[0];
    G(ctor, t);
    t[0] = (uint64_t)(uintptr_t)vt;
    EXPECT("Start(0, 0)", GB(start, t, 0, 0), 1);
    EXPECT("IsAlive() while Run waits", GB(is_alive, t), 1);
    EXPECT("Join(30) while Run waits", GB(join, t, 30), 0);
    uint64_t h = G(get_handle, t);
    uint32_t id = (uint32_t)G(get_id, t);
    EXPECT_TRUE("GetThreadHandle() and GetThreadID() of a running thread", h != 0 && id != 0 && id != RT_FIRST_THREAD_ID);
    DWORD code = 0;
    EXPECT_TRUE("the handle is a thread's, and the thread is still running (exit code 259)", rt_thread_get_exit_code((HANDLE)(uintptr_t)h, &code) && code == 259);
    EXPECT("GetPriority() of a thread started with priority 0", GI(get_pri, t), 0);
    EXPECT("SetPriority(1)", GB(set_pri, t, 1), 1);
    EXPECT("GetPriority() after it", GI(get_pri, t), 1);
    G(ev_set, ev);
    EXPECT("Join(60000) after Run was let go", GB(join, t, 60000), 1);
    EXPECT("Run ran on the thread GetThreadID named", cbs[0].thread, id);
    EXPECT("GetResult(): Run's wait succeeded", GI(get_result, t), 77);
    EXPECT("IsAlive() after the Join", GB(is_alive, t), 0);
    G(slot((uint64_t)(uintptr_t)t, 0), t, 0);
}

/* ---- 79. A thread-local logging state ----------------------------------------------------------- */
/* Source: the public logging.h's description of PushLoggingState(bThreadLocal = true): the
 * pushed state applies to the calling thread only.  A second thread pushes a cleared
 * thread-local state: its messages reach no listener, the first thread's still do. */

typedef struct { GUESTFN push, pop, msg; char *text; unsigned in_state, after_pop; } TlState;
static void tl_state_thread(Call *c, void *arg) {
    TlState *s = arg;
    G(s->push, 1, 1);
    G(s->msg, s->text);
    s->in_state = wide_rec.calls;
    G(s->pop, 1);
    G(s->msg, s->text);
    s->after_pop = wide_rec.calls;
}

static void run_79(Call *c) {
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(push, "LoggingSystem_PushLoggingState");
    T0(pop, "LoggingSystem_PopLoggingState");
    T0(msg, "Msg");
    G(reg_listener, gate2_listener(&wide_rec));
    TlState s = { push, pop, msg, gdup("t\n"), 99, 99 };
    G(msg, s.text);
    EXPECT("first thread: messages received", wide_rec.calls, 1);
    if (!gate2_on_thread(c, tl_state_thread, &s, NULL)) return;
    EXPECT("second thread, inside its own cleared state: messages received so far", s.in_state, 1);
    EXPECT("second thread, after its pop", s.after_pop, 2);
    G(msg, s.text);
    EXPECT("first thread afterwards", wide_rec.calls, 3);
}

/* ---- 80. Messages from two threads at once ---------------------------------------------------- */
/* Source: an invariant: every message is delivered once.  Two threads send 3,000 formatted
 * messages each; the listener (called under the logging system's own lock) must count 6,000,
 * and the last text each thread sent must have arrived whole. */

typedef struct { GUESTFN msg; int tag; } SpewJob;
static void spew_loop(Call *c, void *arg) {
    SpewJob *j = arg;
    char *fmt = gdup("thread %d message %d of %s %.2f\n"), *s = gdup("gate2");
    for (int i = 0; i < 3000; i++) G(j->msg, fmt, j->tag, i, s, dbits(i * 0.25));
}

static void run_80(Call *c) {
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(msg, "Msg");
    G(reg_listener, gate2_listener(&wide_rec));
    SpewJob a = { msg, 1 }, b = { msg, 2 };
    void *t = gate2_thread_start(c, spew_loop, &b);
    if (!t) return;
    spew_loop(c, &a);
    if (!gate2_thread_finish(c, t)) return;
    EXPECT("messages received from two threads sending 3,000 each", wide_rec.calls, 6000);
    for (int i = 0; i < GATE2_LISTENER_KEPT; i++) {
        int tag = -1, n = -1; char word[16] = ""; double v = -1;
        if (sscanf(wide_rec.text[i], "thread %d message %d of %15s %lf", &tag, &n, word, &v) != 4 || (tag != 1 && tag != 2) || strcmp(word, "gate2") != 0 || v != n * 0.25)
            FAILF("one of the first messages arrived as \"%.80s\"", wide_rec.text[i]);
    }
}

/* ---- 81. CCommandLine: AppendParm and RemoveParm ------------------------------------------------ */
/* Source: the public icommandline.h, as a guess at slots 6 and 5 (finding 21 left them
 * undecoded; the order of the public header puts RemoveParm and AppendParm there), confirmed
 * by behaviour: an appended parameter is found with its value, a removed one is not, and the
 * others are untouched. */

static void run_81(Call *c) {
    T0(command_line, "CommandLine");
    uint64_t cl = G(command_line);
    G(slot(cl, 1), cl, gdup("game.exe -keep 1 -drop 2 +last three"));
    EXPECT_TRUE("FindParm(-drop) before", GI(slot(cl, 11), cl, gdup("-drop")) > 0);
    G(slot(cl, 6), cl, gdup("-added"), gdup("val"));                           /* AppendParm(parm, value) */
    EXPECT_TRUE("FindParm(-added) after AppendParm", GI(slot(cl, 11), cl, gdup("-added")) > 0);
    EXPECT_STR("ParmValue(-added, \"x\")", G(slot(cl, 9), cl, gdup("-added"), gdup("x")), "val");
    EXPECT("ParmValue(-keep, 9) is untouched", GI(slot(cl, 8), cl, gdup("-keep"), 9), 1);
    G(slot(cl, 5), cl, gdup("-drop"));                                          /* RemoveParm(parm) */
    EXPECT("FindParm(-drop) after RemoveParm", GI(slot(cl, 11), cl, gdup("-drop")), 0);
    EXPECT_STR("ParmValue(+last, \"x\") is untouched", G(slot(cl, 9), cl, gdup("+last"), gdup("x")), "three");
    EXPECT_STR("ParmValue(-added, \"x\") is untouched", G(slot(cl, 9), cl, gdup("-added"), gdup("x")), "val");
    EXPECT_TRUE("the line no longer holds -drop", strstr(gstr(G(slot(cl, 3), cl)), "-drop") == NULL);
    EXPECT_TRUE("the line holds -added", strstr(gstr(G(slot(cl, 3), cl)), "-added") != NULL);
}

/* ---- 82. CreateSimpleThread with a routine of the driver's ----------------------------------- */
/* Source: Windows thread semantics through the wrappers rank 27 found: the routine receives
 * the parameter, its return value is the exit code, and a join with a short timeout fails
 * while the routine has not returned. */

static void run_82(Call *c) {
    T0(create, "CreateSimpleThread");
    T0(join, "ThreadJoin");
    T0(release, "ReleaseThreadHandle");
    T0(sleep, "ThreadSleep");
    T0(ev_ctor, "??0CThreadEvent@@QEAA@_N@Z");
    T0(ev_set, "?Set@CThreadEvent@@QEAA_NXZ");
    T0(ev_wait, "?Wait@CThreadEvent@@QEAA_NI@Z");
    uint64_t *ev = gate2_mem(16);
    G(ev_ctor, ev, 1);
    RunWait w = { ev_wait, ev };
    cbs[1] = (Cb){ .hook = run_wait_hook, .user = &w };
    uint64_t h = G(create, cb_stub[1], 0xABCDEF0123ull, 0);
    EXPECT_TRUE("CreateSimpleThread returns a handle", h != 0);
    EXPECT("ThreadJoin(handle, 30) while the routine waits", GB(join, h, 30), 0);
    G(sleep, 0);
    G(ev_set, ev);
    EXPECT("ThreadJoin(handle, 60000) after the routine was let go", GB(join, h, 60000), 1);
    EXPECT("calls of the routine", cbs[1].calls, 1);
    EXPECT("the routine's parameter", cbs[1].a[0], 0xABCDEF0123ull);
    DWORD code = 0;
    EXPECT_TRUE("the exit code is what the routine returned", rt_thread_get_exit_code((HANDLE)(uintptr_t)h, &code) && code == 77);
    EXPECT("ReleaseThreadHandle", GB(release, h), 1);
}

/* ---- 83. Readers do not exclude readers ----------------------------------------------------- */
/* Source: the invariant of a reader/writer lock that rank 34 and 35 leave out: while one
 * thread holds the lock for reading, another can take it for reading too; and after two
 * threads took and gave it back 20,000 times each, a writer gets in at once. */

static void run_83(Call *c) {
    T0(s_ctor, "??0CThreadSpinRWLock@@QEAA@XZ");
    T0(s_rd, "?LockForRead@CThreadSpinRWLock@@QEAAXXZ");
    T0(s_un_rd, "?UnlockRead@CThreadSpinRWLock@@QEAAXXZ");
    T0(s_try_wr, "?TryLockForWrite@CThreadSpinRWLock@@QEAA_NXZ");
    T0(ctor, "??0CThreadRWLock@@QEAA@XZ");
    T0(rd, "?LockForRead@CThreadRWLock@@QEAAXXZ");
    T0(un_rd, "?UnlockRead@CThreadRWLock@@QEAAXXZ");
    T0(wr, "?LockForWrite@CThreadRWLock@@QEAAXXZ");
    T0(un_wr, "?UnlockWrite@CThreadRWLock@@QEAAXXZ");
    uint32_t *spin = gfill(16, 0xEE);
    uint8_t *lock = gfill(0x200, 0xEE);
    G(s_ctor, spin);
    G(ctor, lock);
    static volatile uint64_t unused;
    BlockJob reader = { s_rd, s_un_rd, spin, 0 };
    G(s_rd, spin);
    if (!gate2_on_thread(c, block_take, &reader, NULL)) return;        /* returns only if the second reader got in */
    EXPECT("spin lock: a second reader got in while the first held it", reader.got, 1);
    EXPECT("spin lock: readers after the second left", spin[0], 1);
    G(s_un_rd, spin);
    CountJob j = { s_rd, s_un_rd, spin, &unused, 20000 };
    void *t = gate2_thread_start(c, count_loop, &j);
    if (!t) return;
    count_loop(c, &j);
    if (!gate2_thread_finish(c, t)) return;
    EXPECT("spin lock: the lock word after 40,000 read locks from two threads", spin[0], 0);
    EXPECT("spin lock: TryLockForWrite afterwards", GB(s_try_wr, spin), 1);
    reader = (BlockJob){ rd, un_rd, lock, 0 };
    G(rd, lock);
    if (!gate2_on_thread(c, block_take, &reader, NULL)) return;
    EXPECT("event lock: a second reader got in while the first held it", reader.got, 1);
    G(un_rd, lock);
    j = (CountJob){ rd, un_rd, lock, &unused, 20000 };
    t = gate2_thread_start(c, count_loop, &j);
    if (!t) return;
    count_loop(c, &j);
    if (!gate2_thread_finish(c, t)) return;
    G(wr, lock);                                /* returns at once if no reader is left counted */
    G(un_wr, lock);
}

/* ---- 84. printf through Msg: flags together, a NULL string, ten arguments ------------------- */
/* Source: the host's snprintf, for conversions on which the two libraries agree; "(null)" for
 * a NULL string is what both print.  Arguments five to eleven travel on the stack. */

static void run_84(Call *c) {
    T0(reg_listener, "LoggingSystem_RegisterLoggingListener");
    T0(msg, "Msg");
    T0(warning, "Warning");
    G(reg_listener, gate2_listener(&wide_rec));
    char want[200];
    rec_clear();
    snprintf(want, sizeof want, "%d %d %d %d %d %d %d %d %d %d\n", 1, -2, 3, -4, 5, -6, 7, -8, 9, -10);
    G(msg, gdup("%d %d %d %d %d %d %d %d %d %d\n"), 1, S32(-2), 3, S32(-4), 5, S32(-6), 7, S32(-8), 9, S32(-10));
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], want) != 0) FAILF("ten integers: %u messages, \"%.80s\"", wide_rec.calls, wide_rec.text[0]);
    rec_clear();
    snprintf(want, sizeof want, "%-+6d|%+06d|%#o|%#X|%x|%-8.3s|%8.3s|\n", 42, 42, 8, 0xabc, 0u, "abcdef", "abcdef");
    G(msg, gdup("%-+6d|%+06d|%#o|%#X|%x|%-8.3s|%8.3s|\n"), 42, 42, 8, 0xabc, 0, gdup("abcdef"), gdup("abcdef"));
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], want) != 0) FAILF("flags together: %u messages, \"%.80s\"; snprintf gives \"%.80s\"", wide_rec.calls, wide_rec.text[0], want);
    rec_clear();
    G(msg, gdup("[%s] [%10s]\n"), 0, 0);
    if (wide_rec.calls != 1 || strcmp(wide_rec.text[0], "[(null)] [    (null)]\n") != 0) FAILF("a NULL string: %u messages, \"%.80s\"", wide_rec.calls, wide_rec.text[0]);
    rec_clear();
    snprintf(want, sizeof want, "%s %.3f %lld %c %5.1f%%\n", "w", 1.0627, 123456789012345LL, 'q', 99.5);
    G(warning, gdup("%s %.3f %lld %c %5.1f%%\n"), gdup("w"), dbits(1.0627), 123456789012345ull, 'q', dbits(99.5));
    unsigned n = wide_rec.calls;
    if (n < 1 || n > 2 || strcmp(wide_rec.text[n - 1], want) != 0) FAILF("Warning with mixed arguments: %u messages, the last \"%.80s\"; expected \"%.80s\"", n, wide_rec.text[n ? n - 1 : 0], want);
}

/* ---- 85. RandomInt over other ranges ------------------------------------------------------------ */
/* Source: finding 21's model of RandomInt (rank 10 checked it for 0 to 99 only): draws above
 * the largest multiple of the range size are rejected, then the draw modulo the size is added
 * to the lower bound; a range of more than 2^31 values returns the lower bound. */

static int32_t rnd_int(int32_t *s, int32_t lo, int32_t hi) {
    uint32_t n = (uint32_t)hi - (uint32_t)lo + 1;
    if (n <= 1 || n - 1 > 0x7fffffffu) return lo;
    uint32_t limit = 0x7fffffffu - 0x80000000u % n, x;
    do x = (uint32_t)rnd_next(s); while (x > limit);
    return (int32_t)((uint32_t)lo + x % n);
}

static void run_85(Call *c) {
    static const int32_t ranges[][2] = { { -100, 100 }, { 0, 1 }, { -5, -5 }, { 0, 0x7ffffffe }, { -0x40000000, 0x3fffffff }, { 1000000, 1000002 },
                                         { 0, 0x60000000 }, { -2147483647 - 1, 2147483647 }, { 10, -10 } };
    VS(g_seed, "RandomSeed");
    VS(g_int, "RandomInt");
    VS(g_get, "GetCurrentRandomSeed");
    for (unsigned r = 0; r < sizeof ranges / sizeof ranges[0]; r++) {
        int32_t lo = ranges[r][0], hi = ranges[r][1], s = (int32_t)(31 + r);
        G(g_seed, S32(s));
        for (int i = 0; i < 3000; i++) {
            int32_t want = rnd_int(&s, lo, hi), got = GI(g_int, S32(lo), S32(hi));
            if (got != want) FAILF("RandomInt(%d, %d), draw %d: got %d, the model gives %d", lo, hi, i, got, want);
        }
        EXPECT("GetCurrentRandomSeed after 3,000 draws", GI(g_get), s);
    }
}

/* ---- 86. The game allocator from two threads -------------------------------------------------- */
/* Source: invariants.  Two threads allocate, fill, check and free 20,000 blocks each, 64 live
 * at a time per thread; no block may ever show another thread's pattern. */

typedef struct { uint64_t al; uint8_t tag; int bad; } AllocJob;
static void alloc_loop(Call *c, void *arg) {
    AllocJob *j = arg;
    uint8_t *live[64] = { 0 };
    uint32_t size[64], seed = 0xA5A5A5A5u ^ j->tag;
    for (int i = 0; i < 20000 && !j->bad; i++) {
        seed = seed * 1664525u + 1013904223u;
        unsigned k = (seed >> 10) & 63;
        if (live[k]) {
            for (uint32_t m = 0; m < size[k]; m++) if (live[k][m] != (uint8_t)(j->tag + k)) { j->bad = i + 1; break; }
            G(slot(j->al, 5), j->al, live[k]);
        }
        size[k] = 1 + (seed >> 16) % ((seed & 7) ? 300 : 9000);
        live[k] = (uint8_t *)(uintptr_t)G(slot(j->al, 1), j->al, size[k]);
        if (!live[k]) { j->bad = -1; break; }
        memset(live[k], j->tag + (int)k, size[k]);
    }
    for (unsigned k = 0; k < 64; k++) if (live[k]) G(slot(j->al, 5), j->al, live[k]);
}

static void run_86(Call *c) {
    T0(create, "CreateGlobalMemAlloc");
    uint64_t al = G(create);
    AllocJob a = { al, 0x10, 0 }, b = { al, 0x80, 0 };
    void *t = gate2_thread_start(c, alloc_loop, &b);
    if (!t) return;
    alloc_loop(c, &a);
    if (!gate2_thread_finish(c, t)) return;
    if (a.bad || b.bad) FAILF("a block was overwritten or an allocation failed (first thread: step %d, second thread: step %d; -1 = NULL)", a.bad, b.bad);
    uint64_t p = G(slot(al, 1), al, 100);
    EXPECT_TRUE("Alloc(100) afterwards", p != 0);
    G(slot(al, 5), al, p);
}

/* ---- 87. KeyValuesSystem from two threads ----------------------------------------------------- */
/* Source: invariants.  Two threads each enter 1,500 names of their own and 500 names both
 * use; every name ends with one symbol that gives the name back, whichever thread asks. */

typedef struct { uint64_t kv; int tag; int bad; int32_t shared[500]; } KvJob;
static void kv_loop(Call *c, void *arg) {
    KvJob *j = arg;
    char *name = gate2_mem(40);
    for (int i = 0; i < 2000 && !j->bad; i++) {
        if (i % 4 == 3) snprintf(name, 40, "gate2_shared_%d", i / 4); else snprintf(name, 40, "gate2_t%d_%d", j->tag, i);
        int32_t sym = GI(slot(j->kv, 3), j->kv, name, 1);
        uint64_t back = G(slot(j->kv, 4), j->kv, S32(sym));
        if (sym < 0 || !back || strcmp(gstr(back), name) != 0) j->bad = i + 1;
        if (i % 4 == 3) j->shared[i / 4] = sym;
    }
}

static void run_87(Call *c) {
    VS(get, "KeyValuesSystem");
    static KvJob a, b;
    a = (KvJob){ .kv = G(get), .tag = 1 };
    b = (KvJob){ .kv = a.kv, .tag = 2 };
    void *t = gate2_thread_start(c, kv_loop, &b);
    if (!t) return;
    kv_loop(c, &a);
    if (!gate2_thread_finish(c, t)) return;
    if (a.bad || b.bad) FAILF("a name did not come back from its symbol (first thread: step %d, second thread: step %d)", a.bad, b.bad);
    for (int i = 0; i < 500; i++) if (a.shared[i] != b.shared[i]) FAILF("shared name %d has symbol %d on one thread and %d on the other", i, a.shared[i], b.shared[i]);
}

/* ---- 88. Two threads passing a turn with events ------------------------------------------------ */
/* Source: Windows auto-reset events: one Set lets exactly one Wait through.  Two threads pass
 * a turn back and forth 3,000 times; a lost or doubled wake-up breaks the alternation. */

typedef struct { GUESTFN set, wait; void *mine, *theirs; volatile int *turn; int me, bad; } PingJob;
static void ping_loop(Call *c, void *arg) {
    PingJob *j = arg;
    for (int i = 0; i < 3000; i++) {
        if (!GB(j->wait, j->mine, 20000)) { j->bad = -(i + 1); return; }
        if (*j->turn != j->me) { j->bad = i + 1; return; }
        *j->turn = !j->me;
        G(j->set, j->theirs);
    }
}

static void run_88(Call *c) {
    T0(ev_ctor, "??0CThreadEvent@@QEAA@_N@Z");
    T0(ev_set, "?Set@CThreadEvent@@QEAA_NXZ");
    T0(ev_wait, "?Wait@CThreadEvent@@QEAA_NI@Z");
    uint64_t *e0 = gate2_mem(16), *e1 = gate2_mem(16);
    static volatile int turn;
    turn = 0;
    G(ev_ctor, e0, 0); G(ev_ctor, e1, 0);
    PingJob a = { ev_set, ev_wait, e0, e1, &turn, 0, 0 }, b = { ev_set, ev_wait, e1, e0, &turn, 1, 0 };
    void *t = gate2_thread_start(c, ping_loop, &b);
    if (!t) return;
    G(ev_set, e0);                              /* the first turn */
    ping_loop(c, &a);
    if (!gate2_thread_finish(c, t)) return;
    if (a.bad || b.bad) FAILF("the alternation broke (first thread: %d, second thread: %d; negative = a wait of 20 s timed out at that turn)", a.bad, b.bad);
    EXPECT("the first event at the end: Wait(0) (the second thread's last Set)", GB(ev_wait, e0, 0), 1);
    EXPECT("the second event at the end: Wait(0)", GB(ev_wait, e1, 0), 0);
}

/* ---- 89. The clocks do not run backwards ---------------------------------------------------- */
/* Source: an invariant of a monotonic clock, on two threads at once: 20,000 readings each of
 * Plat_FloatTime, Plat_MSTime and Timer_GetTimeUS never decrease on a thread, and
 * Plat_GetClockStart stays what it was. */

typedef struct { GUESTFN f, ms, us; int bad; } ClockJob;
static void clock_loop(Call *c, void *arg) {
    ClockJob *j = arg;
    double f0 = 0; uint32_t m0 = 0; int64_t u0 = 0;
    for (int i = 0; i < 20000; i++) {
        double f = rt_guest_call_fp(c->ctx, j->f, NULL, 0);
        uint32_t m = (uint32_t)G(j->ms);
        int64_t u = (int64_t)G(j->us);
        if (f < f0) { j->bad = 1; return; }
        if (m < m0) { j->bad = 2; return; }
        if (u < u0) { j->bad = 3; return; }
        f0 = f; m0 = m; u0 = u;
    }
}

static void run_89(Call *c) {
    T0(f, "Plat_FloatTime");
    T0(ms, "Plat_MSTime");
    T0(us, "Timer_GetTimeUS");
    T0(start, "Plat_GetClockStart");
    rt_guest_call_fp(c->ctx, f, NULL, 0);
    uint64_t s0 = G(start);
    ClockJob a = { f, ms, us, 0 }, b = { f, ms, us, 0 };
    void *t = gate2_thread_start(c, clock_loop, &b);
    if (!t) return;
    clock_loop(c, &a);
    if (!gate2_thread_finish(c, t)) return;
    if (a.bad || b.bad) FAILF("a clock went backwards (1 Plat_FloatTime, 2 Plat_MSTime, 3 Timer_GetTimeUS): first thread %d, second thread %d", a.bad, b.bad);
    EXPECT("Plat_GetClockStart() afterwards", G(start), s0);
}

/* ---- 90. A hundred logging channels ------------------------------------------------------------ */
/* Source: invariants, as rank 45, at a size that fills a good part of the table: every name
 * gets its own id in order, is found again in either case, and keeps its own colour. */

static void run_90(Call *c) {
    T0(reg, "LoggingSystem_RegisterLoggingChannel");
    T0(find, "LoggingSystem_FindChannel");
    T0(count, "LoggingSystem_GetChannelCount");
    T0(get_colour, "LoggingSystem_GetChannelColor");
    T0(enabled, "LoggingSystem_IsChannelEnabled");
    int32_t n0 = GI(count);
    char *name = gate2_mem(32);
    for (int i = 0; i < 100; i++) {
        snprintf(name, 32, "gate2_many_%03d", i);
        EXPECT("the id of each new channel", GI(reg, name, 0, 0, i % 4, 0x1000 + i), n0 + i);
    }
    EXPECT("GetChannelCount after 100 registrations", GI(count), n0 + 100);
    for (int i = 0; i < 100; i++) {
        snprintf(name, 32, i & 1 ? "GATE2_MANY_%03d" : "gate2_many_%03d", i);
        EXPECT("FindChannel of each", GI(find, name), n0 + i);
        EXPECT("GetChannelColor of each", (uint32_t)G(get_colour, S32(n0 + i)), 0x1000 + (uint32_t)i);
        for (int sev = 0; sev < 4; sev++) EXPECT("IsChannelEnabled(each, severity)", GB(enabled, S32(n0 + i), sev), sev >= i % 4);
    }
}

/* ---- the rows -------------------------------------------------------------------------------- */

#define ROW(n, needs, title, call, check, source) { n, title, needs, call, check, source, NULL, run_##n }
#define LOADED (NEEDS_IMPORTS | NEEDS_TLS | NEEDS_ENTRY)

const Entry gate2_calls_wide[] = {
    ROW(31, LOADED, "Server-frame and loading-thread identity: DeclareCurrentThreadIsServerFrameThread, DeclareCurrentThreadNotServerFrameThread, ThreadInServerFrameThread, ThreadInMainOrServerFrameThread, ThreadCouldDoServerWork, SetLoadingThreadDuringVideo, ThreadInMainLoadingThreadDuringVideo",
        "Ask each query on the main thread and on a second thread, before and after each declares itself.",
        "The declared thread is the server frame thread; with none declared only the main thread could do server work.",
        "The names, checked against the instructions of each (5 to 22)."),
    ROW(32, LOADED, "Thread handle helpers: ThreadGetCurrentHandle, ThreadThisThreadIsMain, ThreadSetPriority, ThreadSetAffinity, ThreadSetDebugName, ReleaseThreadHandle",
        "Each once or twice, on the calling thread and on a thread made by CreateSimpleThread.",
        "The pseudo-handle -2; the thread's id; priorities read back; the affinity mask before; CloseHandle's TRUE then FALSE.",
        "Windows: each export wraps one kernel32 function (read: 1 to 16 instructions)."),
    ROW(33, LOADED, "CThreadSpinRWLock: LockForRead, LockForWrite, the const and _UnforcedInline spellings, operator=",
        "Lock and unlock through every exported spelling on one thread.",
        "The lock word counts readers and shows the writer as rank 6 found; every spelling behaves alike.",
        "Invariants of a reader/writer lock; the lock word's layout from rank 6."),
    ROW(34, LOADED, "CThreadSpinRWLock between two threads: SpinLockForRead, SpinLockForWrite",
        "Two threads add to one counter under the write lock; a reader waits for a writer and a writer for a reader.",
        "No addition is lost; the waiting thread gets in only after the unlock.",
        "Invariant: mutual exclusion."),
    ROW(35, LOADED, "CThreadRWLock: constructor, LockForRead, UnlockRead, LockForWrite, UnlockWrite (both spellings), destructor",
        "As rank 34, on the lock that waits on events instead of spinning.",
        "No addition is lost; the waiting thread gets in only after the unlock.",
        "Invariant: mutual exclusion."),
    ROW(36, LOADED, "CThreadMutex: Lock const, Unlock const, TryLock const, LockSilent, UnlockSilent, AssertOwnedByCurrentThread",
        "The spellings rank 11 leaves out, then two threads against one mutex.",
        "The critical section's recursion count and owner follow; no addition is lost.",
        "Windows CRITICAL_SECTION fields; invariant: mutual exclusion."),
    ROW(37, LOADED, "CThreadFullMutex: constructor, default constructor closure, Lock, Lock(timeout), Unlock, Release, AssertOwnedByCurrentThread, SetTrace, destructor",
        "Lock twice, release from a thread that does not own it, release twice, release once more; a second thread takes the free mutex.",
        "Release succeeds only for the owner and only while held; a second thread cannot take an owned mutex.",
        "Windows mutex semantics (CreateMutex, ReleaseMutex)."),
    ROW(38, LOADED, "CThreadEvent: auto-reset, default constructor closure, the named constructor, CheckNamedEvent",
        "An auto-reset event is set and checked twice; a named manual-reset event is looked up by name while set, while reset, and after it is destroyed.",
        "One Check consumes an auto-reset event; CheckNamedEvent tells signalled (2), not signalled (1) and no such event (0) apart; a named event outlives its object (the destructor does not close it).",
        "Windows event semantics; the result values from the public threadtools.h, as a guess."),
    ROW(39, LOADED, "WaitForMultiple: CThreadSyncObject and CThreadEvent, array of pointers and array of objects",
        "Three manual-reset events with none, one, two and all set; wait for any and for all.",
        "The lowest signalled index, or 0x102; with wait-all, 0 only when all are set.",
        "WaitForMultipleObjects as documented."),
    ROW(40, LOADED, "CThreadSyncObject: constructor, operator!, GetHandle, AssertUseable, destructor",
        "On an empty object and on an event.",
        "Empty: no handle and operator! true. Event: its handle and false; the destructor closes the handle.",
        "The mangled names and an invariant."),
    ROW(41, LOADED, "CThreadLocalBase: constructor, Set, Get, operator=, destructor",
        "Two objects; set and get on two threads.",
        "Each thread reads its own value; a thread that set nothing reads NULL.",
        "Windows TLS semantics."),
    ROW(42, LOADED, "CThread, not started: constructor, GetThreadProc, GetThreadID, GetThreadHandle (both), IsAlive, SetName, GetName, Init, OnExit, destructor",
        "Construct in guest memory, ask everything that does not need a running thread, destroy through the vtable.",
        "The exported vtable and ThreadProc; no handle, no id, not alive; the name reads back.",
        "The export table; a round trip."),
    ROW(43, LOADED, "CThread::Start, Join, GetResult, GetCurrentCThread with a Run of the driver's",
        "A CThread whose vtable copy has a native stub in Run's slot; Start(0, 0), Join, GetResult.",
        "Run is called once, on a new thread, with the object; GetResult is what it returned.",
        "What a thread class must do; priority 0 from the public header."),
    ROW(44, LOADED, "CThread statics: Sleep, Yield, CalcStackDepth, IsSuspended",
        "Sleep(25), 100 Yields, CalcStackDepth of two addresses.",
        "At least 24 ms; the depths differ by the distance of the addresses.",
        "Windows Sleep; three instructions read."),
    ROW(45, LOADED, "Logging channels: LoggingSystem_RegisterLoggingChannel, LoggingSystem_GetFirstChannelID, LoggingSystem_GetNextChannelID, LoggingSystem_GetChannel",
        "Register two channels, one of them twice; find them; walk all ids; read the two records.",
        "New ids follow the count; a name is one channel; the walk visits every id once; the record holds id, severity, colour, name.",
        "Invariants; the record's layout from the public logging.h, as a guess."),
    ROW(46, LOADED, "Logging channels: colour, flags, spew level, tags (LoggingSystem_Get/SetChannelColor, Get/SetChannelFlags, SetChannelSpewLevel, ...ByName, ...ByTag, IsChannelEnabled, AddTagToCurrentChannel, HasTag)",
        "A channel registered with a tags function that adds two tags, and one without.",
        "The tags function runs once; setters read back; a channel is enabled at or above its minimum severity; by-tag reaches only tagged channels.",
        "Round trips; meanings from the public logging.h, as a guess."),
    ROW(47, LOADED, "The logging state stack: LoggingSystem_PushLoggingState, LoggingSystem_PopLoggingState, LoggingSystem_ResetCurrentLoggingState",
        "Msg with a listener registered, inside a cleared pushed state, after the pop, inside a copied state, after a reset, after the pop.",
        "The listener receives the message exactly where its registration is in force.",
        "The public logging.h's description, as a guess, checked by behaviour."),
    ROW(48, LOADED, "Logging entry points: LoggingSystem_Log (both exports), LoggingSystem_LogDirect, LoggingSystem_LogV",
        "One message through each, on a channel of the driver's, with a severity and a colour.",
        "The listener gets the formatted text, the channel, the severity and the colour; each returns 0.",
        "The host's snprintf for the text; the arguments passed for the rest."),
    ROW(49, LOADED, "LoggingSystem_SetLoggingResponsePolicy with a policy of the driver's",
        "Install an object whose vtable slots are native stubs; log a warning and a message.",
        "Slot 1 is called once per message with the message's context; the log call returns what it returned.",
        "The public logging.h, as a guess, confirmed by the calls."),
    ROW(50, LOADED, "DevMsg, DevWarning, ConDMsg, ConColorMsg",
        "One formatted message through each, with a listener.",
        "What is delivered is the formatted text; ConColorMsg's colour arrives in the context.",
        "The host's snprintf."),
    ROW(51, LOADED, "printf through Msg: integer, character and string conversions",
        "Ten formats with flags, widths, precisions and length modifiers; strings; %I64d, %p, %ls; a 300-character line.",
        "The listener receives exactly what the host's snprintf produces (Microsoft's own conversions: as documented).",
        "The C standard's printf through the host's snprintf."),
    ROW(52, LOADED, "printf through Msg: floating-point conversions",
        "Twelve formats of %f, %e and %g with widths and precisions, infinity, and doubles mixed with integers and strings.",
        "The host's snprintf with exponents widened to three digits; 1.#INF00 for infinity.",
        "The C standard's printf through the host's snprintf, and the Visual Studio 2012 library's documented differences."),
    ROW(53, LOADED, "ErrorCountReset, ActionableErrorCountGet",
        "Two warnings, a reset, one more warning, with a listener.",
        "The prefixes count 1, 2 and, after the reset, 1 again; the reset announces itself; the actionable count is 0 after it.",
        "Rank 26 and the six instructions of ErrorCountReset."),
    ROW(54, LOADED, "Print_SetClientTime, Print_SetServerTime, Print_ClearClientTime, Print_ClearServerTime, Print_GetTimeStamp, Print_EnableTimePrefix",
        "Set and clear the two time stamps (float arguments) and read the stamp in each state.",
        "The client's if set, else the server's, else false and nothing written.",
        "The instructions of each (2 to 14)."),
    ROW(55, LOADED, "Fourteen exports that are a single ret: _AssertValid*Ptr, _Error/_Warning_AlwaysSpewCallStack_*, Plat_DebugString, PublishAll, PublishAllMiniProfilers, SetStackTranslationSymbolSearchPath, StackToolsNotify_LoadedLibrary, ThreadSetDebugName",
        "Each with pointers into a guarded buffer.",
        "Each returns; the buffer is untouched.",
        "The instructions: one each."),
    ROW(56, LOADED, "Call-stack tools: GetCallStack, GetCallStack_Fast, GetFileAndLineFromAddress, GetModuleNameFromAddress, GetSymbolNameFromAddress, TranslateStackInfo, _CCallStackStatsGatherer_Internal_DumpStatsToFile",
        "Each with a guarded buffer, the name functions also with a size of 0.",
        "No frames, no names: 0 or false, and an output buffer of at least one byte becomes the empty string.",
        "The instructions of each (2 to 5)."),
    ROW(57, LOADED, "Callbacks: SetRemoteErrorReportCallback / SendRemoteErrorReport, SetRemoteMinidumpSendCallback / SendRemoteMinidump, SetIsKitOutsideOfStudioLANCallback / IsKitOutsideOfStudioLAN",
        "Each sender with no callback, with a native stub installed, and after it is removed.",
        "No callback: 0. With one: it is called once with the sender's arguments and its result comes back.",
        "The instructions of each (2 to 10); the senders end in a jump through a register."),
    ROW(58, LOADED, "CUniformRandomStream: GenerateRandomNumber, copy constructor, operator=, destructor; InstallUniformRandomStream",
        "6,000 raw draws from three seeds; a copy and an assigned object; the global functions with the object installed and after Install(NULL).",
        "Every draw equals the generator's model; copies continue the sequence; the global functions use the installed stream.",
        "Finding 21's model of the generator (confirmed by rank 10); the public random.h for Install."),
    ROW(59, LOADED, "RandomFloat: the method and the global function",
        "12,000 draws over six ranges, and the largest state.",
        "Bit for bit the single-precision model: (float)draw / 2147483647, capped below 1, scaled into the range.",
        "An independent computation; the formula from the public random.cpp, agreed by the 23 instructions."),
    ROW(60, LOADED, "RandomFloatExp: the method and the global function",
        "5,000 draws with exponents 1, 2, 0.5, 3.7 and 0.25.",
        "The unit draw raised to the exponent, within one unit in the last place of the host's powf; exact for exponent 1.",
        "An independent computation with the host's powf."),
    ROW(61, LOADED, "CGaussianRandomStream (constructor, closure, copy, operator=, AttachToStream, RandomFloat, destructor) and RandomGaussianFloat",
        "20,000 draws with mean 3 and deviation 2; the same seed on a second stream; a copy; the global function after reseeding.",
        "Sample mean and deviation within 0.06; the same seed repeats the sequence bit for bit.",
        "Invariants; the method is not assumed."),
    ROW(62, LOADED, "Coroutines: Coroutine_Create, Coroutine_Continue, Coroutine_YieldToMain, Coroutine_IsActive, Coroutine_GetCurrentlyActive, Coroutine_ReleaseThreadMemory",
        "A coroutine whose body is a native stub that yields twice; Continue three times.",
        "Continue returns true, true, false; the body advances one stage each time and sees itself active.",
        "The public vstdlib/coroutine.h."),
    ROW(63, LOADED, "KeyValuesSystem with 3,000 symbols, and AllocKeyValuesMemory / FreeKeyValuesMemory / RegisterSizeofKeyValues",
        "3,000 names in, each looked up both ways; 200 blocks from the pool written, checked and freed.",
        "Distinct names have distinct symbols that give the names back; blocks do not overlap.",
        "Invariants."),
    ROW(64, LOADED, "CreateNewThreadPool, DestroyThreadPool, g_pThreadPool",
        "Create two pools, look at the global one, destroy the two.",
        "Two distinct objects with finding 21's vtable, a third behind the data export; no thread is started or left.",
        "Finding 21 section 6.2 for the vtable; an invariant."),
    ROW(65, LOADED, "CCommandLine: CreateCmdLine(argc, argv), CheckParm, ParmValue with a float default",
        "Eleven arguments; every parameter read back; CheckParm with and without a value pointer; four float values and a default.",
        "The line is each argument in quotes; parameters read back without them; values convert exactly (2.75, 1500, 640).",
        "The public icommandline.h with finding 21's slots; the C library's atof."),
    ROW(66, LOADED, "The game allocator under load: 3,000 live blocks of 1 byte to 470 KB, Realloc of half of them, frees in another order",
        "Alloc, GetSize, fill; check; free every second; Realloc the others; refill the gaps; check; free all.",
        "No block is ever overwritten by another; sizes are at least what was asked; Realloc keeps the prefix.",
        "Invariants of any allocator."),
    ROW(67, LOADED, "Allocator entries with a file and a line: IMemAlloc slots 0, 2, 4 and CAlignedNewDelete<8> operator new / delete (size, int, file, line), operator=",
        "One block through Alloc, Realloc and Free with file and line; sixteen through the operators.",
        "Usable, 8-aligned, not overlapping; Realloc keeps the contents.",
        "Invariants; types from the mangled names."),
    ROW(68, LOADED, "Plat_GetModuleFilename, Plat_IsInDebugSession",
        "Each once, and the kernel32 function it wraps called directly.",
        "The same name and the same answer.",
        "Windows, through the runtime's own exports."),
    ROW(69, LOADED, "GetCurrentDate, GetCurrentDayOfTheWeek, GetCurrentDayOfTheYear, Plat_GetTimeString",
        "The three date functions once; the time string of a fixed struct tm into 64 and into 11 bytes.",
        "The host's calendar in the runtime's zone; asctime's text, cut to the size with a terminator.",
        "The host's gmtime_r and asctime_r."),
    ROW(70, LOADED, "BasicStatStructFieldDesc, CallStackStatStructDescFuncs (constructor, copy, operator=) and CCallStackStorage with a capture function of the driver's",
        "Construct, copy and assign each; the storage's constructor is given a native stub that reports three frames.",
        "The exported vtables are installed; copies equal originals; the capture function is called once and its frames and count are kept.",
        "The export table; invariants; the public header for the capture function."),
    ROW(71, LOADED, "Plat_gmtime and Plat_ConvertToLocalTime over the whole range",
        "20,000 times from 1970 to 3000, 3,000 of them also as local time, and 388 around leap days.",
        "Every field equals the host's gmtime_r.",
        "The host's gmtime_r."),
    ROW(72, LOADED, "EncodeBinaryToString / DecodeBinaryFromString with 400 random buffers",
        "Sizes 0 to 2048; random bytes, all 0xFF, all 0, all 0x80.",
        "The length rule of rank 3; no output byte below 0x80; decoding gives the bytes back.",
        "Invariant: a round trip."),
    ROW(73, LOADED, "UTF-8 and UTF-16 conversion with 300 random strings",
        "Up to 40 random code points of every length class, through the four conversions.",
        "Equal to the driver's own encoders; counts include the terminator.",
        "An independent computation: the Unicode standard's two encodings."),
    ROW(74, LOADED, "Events and semaphores across threads",
        "A thread waits on an event (auto and manual reset) that another sets 40 ms later; a semaphore release past its maximum; a thread woken by a release.",
        "The waiter wakes after the set, not before; the refused release changes nothing.",
        "Windows event and semaphore semantics."),
    ROW(75, LOADED, "Coroutine bookkeeping without a switch: Coroutine_Create, Coroutine_IsActive, Coroutine_GetCurrentlyActive, Coroutine_ValidateGlobals",
        "Create two coroutines and never continue them.",
        "Two handles; no body runs; nothing is active.",
        "The public vstdlib/coroutine.h; invariants."),
    ROW(76, LOADED, "The job system's FIFO lock: JT_InitFifoLock, JT_AcquireFifoLockOrHelp, JT_ReleaseFifoLock (and JT_GetCurrentJob, JT_ThreadIndexEnd)",
        "Initialise; acquire and release 1,001 times on one thread; two threads with job-thread indices 2 and 3 add to one counter under it.",
        "0x1c in the qword and the 64 words; uncontended rounds leave the lock as initialised; no addition is lost between two threads.",
        "Nine instructions for the initial state; invariants for the rest."),
    ROW(77, LOADED, "V_UTF8_strncpy with 600 random strings and lengths",
        "Strings of 1- to 4-byte characters, cut at random lengths.",
        "The longest prefix of whole characters that fits, terminated; nothing written past maxLen.",
        "An independent computation of finding 21's rule."),
    ROW(78, LOADED, "CThread while its thread runs: IsAlive, Join with a timeout, GetThreadHandle, GetThreadID, GetPriority, SetPriority",
        "A CThread whose Run (a native stub) waits on an event; ask, change the priority, set the event, join.",
        "Alive with a handle and an id; Join(30) fails; the priority reads back; after the event the Join succeeds.",
        "What a thread class must say; Windows thread priorities."),
    ROW(79, LOADED, "A thread-local logging state: LoggingSystem_PushLoggingState(true, true) on a second thread",
        "The second thread pushes a cleared thread-local state, logs, pops, logs; the first thread logs before and after.",
        "Only the message inside the second thread's own state is not delivered.",
        "The public logging.h's description, checked by behaviour."),
    ROW(80, LOADED, "Msg from two threads at once",
        "3,000 formatted messages from each of two threads, with a listener.",
        "6,000 deliveries, each text whole.",
        "Invariant: every message is delivered once."),
    ROW(81, LOADED, "CCommandLine: AppendParm and RemoveParm (slots 6 and 5)",
        "Append a parameter with a value, remove another.",
        "The appended one is found with its value, the removed one is gone, the others are untouched.",
        "The public icommandline.h as a guess at the slots, confirmed by behaviour."),
    ROW(82, LOADED, "CreateSimpleThread with a routine of the driver's; ThreadJoin with a timeout",
        "A routine that waits on an event; join for 30 ms, set the event, join.",
        "The routine gets its parameter; the short join fails; the exit code is the routine's result.",
        "Windows thread semantics."),
    ROW(83, LOADED, "Readers do not exclude readers: CThreadSpinRWLock and CThreadRWLock",
        "A second thread takes the read lock while the first holds it; then 20,000 read locks from each of two threads.",
        "The second reader gets in; afterwards the lock is free for a writer.",
        "Invariant of a reader/writer lock."),
    ROW(84, LOADED, "printf through Msg and Warning: ten arguments, flags together, a NULL string, mixed types",
        "Four more formats.",
        "The host's snprintf; (null) for a NULL string.",
        "The C standard's printf through the host's snprintf."),
    ROW(85, LOADED, "RandomInt over nine other ranges",
        "3,000 draws each, among them negative bounds, 2^31 - 1 values, the whole int range and an empty range.",
        "Every draw equals finding 21's model.",
        "Finding 21's model (rank 10 checked 0 to 99 only)."),
    ROW(86, LOADED, "The game allocator from two threads",
        "Each thread allocates, fills, checks and frees 20,000 blocks, 64 live at a time.",
        "No block ever holds anything but its own pattern.",
        "Invariants of any allocator."),
    ROW(87, LOADED, "KeyValuesSystem from two threads",
        "Each thread enters 1,500 names of its own and the same 500 shared names.",
        "Every name gives itself back; a shared name has one symbol.",
        "Invariants."),
    ROW(88, LOADED, "Two threads passing a turn with auto-reset events",
        "3,000 exchanges: wait for my event, take the turn, set the other's.",
        "The turn alternates; no wait times out.",
        "Windows auto-reset event semantics."),
    ROW(89, LOADED, "The clocks on two threads: Plat_FloatTime, Plat_MSTime, Timer_GetTimeUS, Plat_GetClockStart",
        "20,000 readings of each on each of two threads.",
        "No reading is below the one before it on the same thread; the clock start does not move.",
        "Invariant of a monotonic clock."),
    ROW(90, LOADED, "A hundred logging channels",
        "Register 100, find each in either case, read colour and enabled severities.",
        "Ids in order; each found; each keeps its colour and minimum severity.",
        "Invariants."),
};
const unsigned gate2_ncalls_wide = sizeof gate2_calls_wide / sizeof gate2_calls_wide[0];
