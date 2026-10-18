/* The locks the game embeds in its own structures (docs/RUNTIME.md section 6.4).  Interface and
 * specification: rt/lock.h.  Layouts: rt/win/sync.h.
 *
 * Four structures, CRITICAL_SECTION, SRWLOCK, CONDITION_VARIABLE and SLIST_HEADER, each kept
 * entirely inside its Windows-sized guest structure.  Nothing here allocates, takes a host
 * lock or touches the handle table, and no state is kept per thread.
 *
 * HOW A THREAD BLOCKS.  The three structures that block do it on one primitive, applied to a
 * 32-bit word inside the guest structure (lock_wait and lock_wake below):
 *
 *   wait(word, v)   if *word == v, sleep until woken; otherwise return at once.  The kernel
 *                   reads the word and puts the thread on the word's queue as ONE step with
 *                   respect to the wake calls on that word.  It may also return for no reason.
 *   wake(word)      wake one thread, or every thread, that is on the word's queue now.
 *
 * Every wait is therefore written as a loop that looks at the state again, and every
 * lost-wake-up argument below has the same shape.  A sleeper only ever waits for a value v of
 * the word that means "you must sleep".  Whoever ends that condition first changes the word
 * (one atomic operation) and THEN calls wake.  If the kernel's read in wait comes before the
 * change, the sleeper is on the queue before the wake call that follows the change, so the
 * wake reaches it.  If it comes after, the word is no longer v and the sleeper does not sleep.
 * What remains to show for each structure is (1) that the word cannot hold v while nobody is
 * going to change it and wake, and (2) which thread a wake of only one thread reaches.  Those
 * are the comments headed "Lost wake-ups".
 *
 * MEMORY ORDERING.  Guest threads normally run in the x86 ordering mode, but test processes
 * and any host thread that calls in do not, so nothing here assumes it.  Every change of a lock
 * word is one atomic read-modify-write with sequentially consistent ordering (on arm64 the
 * casal, swpal, ldaddal and caspal instructions, which are both an acquire and a release).
 * That gives the two things a lock owes its user: what the releasing thread wrote before it
 * released (the guest's protected data, and our own bookkeeping fields) is visible to the
 * thread whose acquiring operation reads the released value; and the acquiring thread's later
 * writes come after them.  Reads made outside a lock are either only hints that the next
 * atomic operation confirms, or are argued where they stand.
 *
 * The uncontended path of every operation is a single atomic read-modify-write (for the
 * no-sleeper wake of a condition variable, a single atomic load) plus plain loads and stores.
 */
#include <errno.h>
#include <os/os_sync_wait_on_address.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core.h"
#include "report.h"
#include "rt/lock.h"

/* synchapi.h.  rt/win/sync.h does not define it (reported); the value is Windows'. */
#ifndef CONDITION_VARIABLE_LOCKMODE_SHARED
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1u
#endif

/* ---- fatal reports ---------------------------------------------------------------------------- */

/* rt_fatal with the guest caller added when the thread is known.  `ctx` may be NULL (the SRW
 * and SList functions have none): the calling host thread's guest thread is used if it has one. */
static _Noreturn void lock_fatal(Ctx *ctx, const char *fmt, ...) __attribute__((format(printf, 2, 3), noinline, cold));
static _Noreturn void lock_fatal(Ctx *ctx, const char *fmt, ...) {
    char buf[2048];
    size_t at = (size_t)snprintf(buf, sizeof buf, "FATAL runtime: ");
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + at, sizeof buf - at - 2, fmt, ap);
    va_end(ap);
    if (w > 0) at = at + (size_t)w < sizeof buf - 2 ? at + (size_t)w : sizeof buf - 3;
    buf[at++] = '\n';
    buf[at] = 0;
    if (!ctx) {
        RtThread *t = rt_thread_self();
        if (t) ctx = &t->ctx;
    }
    if (ctx) {
        RtThread *t = RT_THREAD(ctx);
        rt_report_context(buf + at, sizeof buf - at, ctx, t && t->shim ? t->shim->entry_rsp : 0);
    }
    rt_report_die(buf);                 /* no lock, one report per process (core/report.h) */
}

/* ---- atomics ------------------------------------------------------------------------------------ */

/* A hint only: the value may be stale by the time it is used. */
static inline uint32_t ld32(const uint32_t *p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }

/* Strong compare-and-swap: fails only if *p != expect, never spuriously.  The try functions
 * depend on that (a spurious failure would be a wrong FALSE). */
static inline bool cas32(uint32_t *p, uint32_t expect, uint32_t desired) {
    return __atomic_compare_exchange_n(p, &expect, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline uint32_t xchg32(uint32_t *p, uint32_t v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }

static inline void cpu_relax(void) { __asm__ __volatile__("yield"); }

/* ---- the wait primitive ------------------------------------------------------------------------- */
/* os_sync_wait_on_address and its wake calls (macOS 14.4), always with size 4 and without the
 * shared-memory flag, on a word that is 4-byte aligned (the kernel refuses any other address
 * with EINVAL, so each structure checks its alignment where it can do so early).
 *
 * What the kernel was seen to do on this machine (macOS 27), which the code relies on:
 *   - a wait whose value does not match returns 0 at once;
 *   - a wake with nobody waiting fails with ENOENT, which is not an error for us;
 *   - a timed wait that runs out fails with ETIMEDOUT, and a timeout of 0 ns is EINVAL (so it
 *     is never passed);
 *   - a wait can fail with EFAULT on a perfectly good address whose page is not resident: the
 *     kernel reads the word with page faults disabled and expects the caller to touch the page
 *     and come back.  It did so for a word in a data page that had never been touched. */

/* A wait returned -1.  Returns if the caller should look at the state again. */
static __attribute__((noinline, cold)) void lock_wait_failed(const uint32_t *word, int err) {
    switch (err) {
    case EINTR:                         /* interrupted: a spurious return */
        return;
    case EFAULT:                        /* not resident: fault the page in ourselves.  If the address */
        (void)*(const volatile uint32_t *)word; /* is really bad this read is the crash that says so */
        return;
    case ENOMEM:                        /* the kernel could not allocate its queue entry: try again */
        sched_yield();
        return;
    default:
        lock_fatal(NULL, "os_sync_wait_on_address on the lock word at %p failed with errno %d.  The word must be "
                   "4-byte aligned guest memory; EINVAL (22) for an aligned word means the kernel's state for "
                   "this address disagrees with a 4-byte private wait", (const void *)word, err);
    }
}

/* Sleep while *word == expected.  May return early for no reason: callers loop. */
static inline void lock_wait(uint32_t *word, uint32_t expected) {
    if (os_sync_wait_on_address(word, expected, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE) < 0) lock_wait_failed(word, errno);
}

/* Monotonic nanoseconds that do not advance while the machine sleeps: the clock the timed
 * wait is anchored to (OS_CLOCK_MACH_ABSOLUTE_TIME). */
static inline uint64_t lock_now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

/* The same with a deadline on lock_now_ns().  Returns false if the deadline has passed, true
 * if the caller should look at the state again (woken, value changed, or no reason). */
static bool lock_wait_until(uint32_t *word, uint32_t expected, uint64_t deadline_ns) {
    uint64_t now = lock_now_ns();
    if (now >= deadline_ns) return false;
    if (os_sync_wait_on_address_with_timeout(word, expected, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                             OS_CLOCK_MACH_ABSOLUTE_TIME, deadline_ns - now) >= 0) return true;
    int err = errno;
    if (err == ETIMEDOUT) return false;
    lock_wait_failed(word, err);
    return true;
}

/* Wake one thread, or all threads, sleeping on `word`.  A wake that is dropped is a lost
 * wake-up, so every failure other than "nobody is waiting" is retried or fatal. */
static __attribute__((noinline)) void lock_wake(uint32_t *word, bool all) {
    for (;;) {
        int r = all ? os_sync_wake_by_address_all(word, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE)
                    : os_sync_wake_by_address_any(word, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE);
        if (r == 0) return;
        int err = errno;
        if (err == ENOENT) return;                  /* nobody is waiting */
        if (err == EINTR) continue;
        if (err == ENOMEM) { sched_yield(); continue; }
        lock_fatal(NULL, "os_sync_wake_by_address_%s on the lock word at %p failed with errno %d",
                   all ? "all" : "any", (void *)word, err);
    }
}

/* ==== CRITICAL_SECTION ============================================================================ */
/* Layout (RUNTIME.md 6.4): DebugInfo -1 once initialised; LockCount -1 free, 0 held;
 * RecursionCount; OwningThread = the owner's thread id; SpinCount; and the low 32 bits of
 * LockSemaphore are the wait word W, the only field that decides who owns the section:
 *
 *     0  free
 *     1  held, and nobody has gone to sleep on it
 *     2  held, and a thread may be asleep on it ("contended")
 *
 * Transitions, each one atomic operation on W:
 *
 *   (a) 0 -> 1   acquire: the fast path of Enter, TryEnter, a spinning thread
 *   (b) x -> 2   a thread on the slow path (an exchange).  If x was 0 it now owns the section,
 *                in state 2; if not, the section is still held by the same owner as before and
 *                is now marked contended, and the thread goes on to wait(W, 2)
 *   (c) x -> 0   release, by the owner only (an exchange); if x was 2 it then wakes ONE sleeper
 *
 * Mutual exclusion.  W != 0 exactly while one thread has acquired and not yet released: the two
 * acquiring steps, (a) and (b) with x = 0, both go from 0 to nonzero in one atomic step, nothing
 * else leaves 0, and only the owner performs (c).
 *
 * Lost wake-ups.  A thread sleeps only in wait(W, 2).  Say the kernel put S on the queue at
 * time t, having read W == 2.  By the above there is an owner at t that has not yet done (c).
 * From t until that release W stays 2, because the only steps possible from 2 are (b), which
 * leaves it 2, and (c).  So the owner's exchange in (c) returns 2 and it calls wake after t,
 * with S (and maybe others) on the queue: one of them is woken.  The woken thread performs (b),
 * unconditionally writing 2: either it gets the section and holds it in state 2, so that its
 * own release wakes the next sleeper, or the section has an owner again and W is 2, which is
 * the situation we started from.  By induction, while any thread is on the queue either W is 2
 * with a live owner, or a woken thread is on its way to (b).  A thread that takes the section
 * by (a) in between (it found W == 0 after a release) does not disturb this: the woken thread
 * still arrives and writes 2.  The price is that the last sleeper holds the section in state 2
 * with nobody behind it, and its release makes one wake call that finds nobody.
 *
 * The other fields.  OwningThread, RecursionCount and LockCount are written only by the owner,
 * after its acquiring operation and before its releasing one, so the lock itself orders all
 * writes to them.  They are read without the lock in one place, the recursion test
 * "OwningThread == my id".  That read can only matter if it returns the reader's own id, and it
 * cannot do so for a thread that is not the owner: that thread's own last write to the field
 * was 0 (in its last Leave), every write of its id precedes that one, and a thread never reads
 * a value older than its own last write to the same location, under any ordering model.  The
 * accesses are relaxed atomics so that a debugger-style reader in another thread is not a data
 * race in C; they compile to plain loads and stores.
 *
 * Windows rules followed: the section is recursive and must be left once per Enter; it must be
 * initialised before use and is unusable after Delete; SetCriticalSectionSpinCount and
 * InitializeCriticalSectionAndSpinCount keep the low 24 bits of the count; a thread that finds
 * the section held spins up to SpinCount times before it sleeps (Windows does so on a
 * multiprocessor machine, which this always is); who gets a contended section is not ordered.
 */

#define CS_FREE        0u
#define CS_HELD        1u
#define CS_CONTENDED   2u
#define CS_SPIN_MASK   0x00FFFFFFu
#define CS_INITIALISED ((PVOID)(intptr_t)-1)

/* LockCount: -1 free, 0 held, the two values rt/lock.h gives.  That is the encoding of Windows
 * before Server 2003 SP1.  Windows 7, which the runtime otherwise reports, keeps "free" in bit
 * 0, so that a held section with no waiters reads -2, and the complement of the number of
 * waiters above bit 1.  Reported; if the guest turns out to read the field, these two
 * constants are the place.  The number of waiters is not reflected either way: keeping it
 * would cost a second atomic operation on the uncontended path. */
#define CS_LOCKCOUNT_FREE  (-1)
#define CS_LOCKCOUNT_HELD  0

static inline uint32_t *cs_word(CRITICAL_SECTION *cs) { return (uint32_t *)&cs->LockSemaphore; }

/* The caller's thread id, read where GetCurrentThreadId reads it.  Id 0 would make a free
 * section (OwningThread 0) look owned by the caller. */
static inline HANDLE cs_self(Ctx *ctx) {
    uint32_t id = LD32(ctx->gs_base + RT_TEB_THREAD_ID);
    if (__builtin_expect(id == 0, 0))
        lock_fatal(ctx, "a critical section was used by a thread whose thread block at %#llx holds thread id 0",
                   (unsigned long long)ctx->gs_base);
    return (HANDLE)(uintptr_t)id;
}

static inline HANDLE cs_owner(CRITICAL_SECTION *cs) { return __atomic_load_n(&cs->OwningThread, __ATOMIC_RELAXED); }

/* Windows rule: a critical section must be initialised before it is used, and must not be used
 * after DeleteCriticalSection.  On Windows 7 entering an all-zero section waits for ever; here
 * it stops.  Anything but the -1 that rt_cs_init stores counts, not only 0: stale memory would
 * otherwise be taken for a lock word. */
static inline void cs_check(Ctx *ctx, CRITICAL_SECTION *cs, const char *api) {
    PVOID info = __atomic_load_n(&cs->DebugInfo, __ATOMIC_RELAXED);
    if (__builtin_expect(info != CS_INITIALISED, 0))
        lock_fatal(ctx, "%s on a critical section at %p that was never initialised, or was deleted (DebugInfo is "
                   "%p; an initialised section has -1)", api, (void *)cs, info);
}

/* The bookkeeping of a first acquisition: the caller has just taken W. */
static inline void cs_take(CRITICAL_SECTION *cs, HANDLE self) {
    __atomic_store_n(&cs->OwningThread, self, __ATOMIC_RELAXED);
    __atomic_store_n(&cs->RecursionCount, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&cs->LockCount, CS_LOCKCOUNT_HELD, __ATOMIC_RELAXED);
}

/* The caller owns the section already. */
static inline void cs_recurse(Ctx *ctx, CRITICAL_SECTION *cs) {
    LONG n = __atomic_load_n(&cs->RecursionCount, __ATOMIC_RELAXED);
    if (__builtin_expect(n <= 0 || n == INT32_MAX, 0))
        lock_fatal(ctx, "critical section at %p: the owner enters it again but RecursionCount is %d (it must be "
                   "between 1 and %d)", (void *)cs, n, INT32_MAX - 1);
    __atomic_store_n(&cs->RecursionCount, n + 1, __ATOMIC_RELAXED);
}

void rt_cs_init(CRITICAL_SECTION *cs, DWORD spin) {
    /* The wait word is cs + 0x18 and must be 4-byte aligned to be waited on.  Windows does not
     * check; a section in a packed structure would work there.  Not handled: stop at the first
     * use rather than at the first contended Enter. */
    if ((uintptr_t)cs & 3)
        lock_fatal(NULL, "InitializeCriticalSection: the critical section at %p is not 4-byte aligned, so its wait "
                   "word cannot be waited on", (void *)cs);
    cs->DebugInfo = CS_INITIALISED;
    cs->LockCount = CS_LOCKCOUNT_FREE;
    cs->RecursionCount = 0;
    cs->OwningThread = NULL;
    cs->LockSemaphore = NULL;                           /* the wait word, and its unused upper half */
    cs->SpinCount = spin & CS_SPIN_MASK;                /* Windows keeps the low 24 bits */
}

void rt_cs_delete(CRITICAL_SECTION *cs) {
    /* Windows: deleting a section that is owned leaves the threads waiting for it in an
     * undefined state.  Here they are not woken (they would take a section that no longer
     * exists) and sleep for ever, which is what Windows 7 does to them. */
    if (cs->DebugInfo == CS_INITIALISED) {
        uint32_t w = ld32(cs_word(cs));
        if (w != CS_FREE || cs_owner(cs) != NULL)
            rt_log(RT_CH_THREAD, RT_LL_WARN, "DeleteCriticalSection(%p): the section is held by thread 0x%x "
                   "(recursion count %d)%s", (void *)cs, (uint32_t)(uintptr_t)cs_owner(cs), cs->RecursionCount,
                   w == CS_CONTENDED ? "; threads may be waiting for it and will never wake" : "");
    }
    memset(cs, 0, sizeof *cs);
}

/* W was not free.  Returns owning the section; the caller does the bookkeeping. */
static __attribute__((noinline)) void cs_enter_slow(CRITICAL_SECTION *cs) {
    uint32_t *w = cs_word(cs);

    /* Windows rule: spin up to SpinCount times before sleeping, but not when other threads are
     * already asleep on the section.  Taking it here is step (a). */
    for (uint32_t spin = (uint32_t)__atomic_load_n(&cs->SpinCount, __ATOMIC_RELAXED) & CS_SPIN_MASK; spin; spin--) {
        uint32_t c = ld32(w);
        if (c == CS_FREE) {
            if (cas32(w, CS_FREE, CS_HELD)) return;
        } else if (c != CS_HELD) {
            break;
        }
        cpu_relax();
    }

    /* Step (b), then sleep while the section is held.  Every pass writes 2 before it waits for
     * 2, so the wait can only sleep on a section that is held and marked contended. */
    while (xchg32(w, CS_CONTENDED) != CS_FREE) lock_wait(w, CS_CONTENDED);
}

void rt_cs_enter(Ctx *ctx, CRITICAL_SECTION *cs) {
    HANDLE self = cs_self(ctx);
    cs_check(ctx, cs, "EnterCriticalSection");
    if (cs_owner(cs) == self) { cs_recurse(ctx, cs); return; }
    if (__builtin_expect(!cas32(cs_word(cs), CS_FREE, CS_HELD), 0)) cs_enter_slow(cs);
    cs_take(cs, self);
}

BOOL rt_cs_try_enter(Ctx *ctx, CRITICAL_SECTION *cs) {
    HANDLE self = cs_self(ctx);
    cs_check(ctx, cs, "TryEnterCriticalSection");
    if (cs_owner(cs) == self) { cs_recurse(ctx, cs); return TRUE; }
    if (!cas32(cs_word(cs), CS_FREE, CS_HELD)) return FALSE;    /* held by another thread: never waits */
    cs_take(cs, self);
    return TRUE;
}

void rt_cs_leave(Ctx *ctx, CRITICAL_SECTION *cs) {
    HANDLE self = cs_self(ctx);
    /* Windows does not check the owner: a Leave by another thread, or one Leave too many,
     * corrupts the section and a later Enter hangs.  Fatal here (rt/lock.h).  A section that
     * was never initialised or was deleted has owner 0 and ends here too. */
    HANDLE owner = cs_owner(cs);
    if (__builtin_expect(owner != self, 0))
        lock_fatal(ctx, "LeaveCriticalSection on a critical section at %p that the calling thread 0x%x does not "
                   "own (OwningThread is 0x%llx, RecursionCount %d, DebugInfo %p)", (void *)cs,
                   (uint32_t)(uintptr_t)self, (unsigned long long)(uintptr_t)owner, cs->RecursionCount, cs->DebugInfo);
    LONG n = __atomic_load_n(&cs->RecursionCount, __ATOMIC_RELAXED) - 1;
    if (n > 0) {
        __atomic_store_n(&cs->RecursionCount, n, __ATOMIC_RELAXED);
        return;
    }
    if (__builtin_expect(n < 0, 0))
        lock_fatal(ctx, "LeaveCriticalSection: the critical section at %p is owned by the caller but its "
                   "RecursionCount is %d", (void *)cs, n + 1);

    /* The bookkeeping goes back to "free" BEFORE the wait word is released: after the release
     * the next owner may already be writing these fields. */
    __atomic_store_n(&cs->RecursionCount, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&cs->OwningThread, NULL, __ATOMIC_RELAXED);
    __atomic_store_n(&cs->LockCount, CS_LOCKCOUNT_FREE, __ATOMIC_RELAXED);

    /* Step (c). */
    uint32_t was = xchg32(cs_word(cs), CS_FREE);
    if (__builtin_expect(was != CS_HELD, 0)) {
        if (was != CS_CONTENDED)
            lock_fatal(ctx, "LeaveCriticalSection: the wait word of the critical section at %p was %#x while the "
                       "caller owned it (it must be 1 or 2): the structure was overwritten", (void *)cs, was);
        lock_wake(cs_word(cs), false);
    }
}

DWORD rt_cs_set_spin(CRITICAL_SECTION *cs, DWORD spin) {
    /* Windows rule: the low 24 bits are stored, and the previous count is returned.  An
     * exchange, so that two threads setting it at once each get a count that was really there. */
    return (DWORD)__atomic_exchange_n(&cs->SpinCount, (ULONG_PTR)(spin & CS_SPIN_MASK), __ATOMIC_RELAXED);
}

/* ==== SRWLOCK ====================================================================================== */
/* The low 32 bits are the word S (RUNTIME.md 6.4); the upper 32 bits are never written and
 * stay as the guest left them.
 *
 *     bit 0        X     held exclusively
 *     bit 1        WAIT  a thread may be asleep on S
 *     bits 2..31   R     number of shared holders
 *
 * Reachable values:  0 (free)   X   X|WAIT   4R (R >= 1)   4R|WAIT.   Free is exactly S == 0:
 * WAIT is only ever set on a held lock, and the release that frees the lock clears it in the
 * same atomic step.  Transitions, each one compare-and-swap:
 *
 *   acquire exclusive   0 -> X
 *   acquire shared      4R -> 4(R+1)             only while neither X nor WAIT is set
 *   mark                s -> s|WAIT              by a thread about to sleep; s is a held value
 *   release exclusive   X -> 0,  X|WAIT -> 0     the second is followed by wake ALL
 *   release shared      4R(|WAIT) -> 4(R-1)(|WAIT) for R > 1;   4 -> 0;   4|WAIT -> 0 followed by
 *                       wake ALL
 *
 * Windows rules followed.  All-zero is an unlocked lock.  The lock has no owner and is not
 * recursive: a second exclusive acquire by the holder deadlocks, as on Windows.  A shared
 * acquire waits not only for an exclusive holder but also whenever a thread is already waiting
 * (WAIT is set) while the lock is held shared, as Windows queues a new reader behind waiters:
 * so writers are not starved by a stream of readers, and a thread that acquires shared a second
 * time while a writer waits deadlocks, which Microsoft documents.  Releasing a lock that is not
 * held in that mode raises STATUS_RESOURCE_NOT_OWNED on Windows; every exception aborts here
 * (RUNTIME.md 9.3), so it is fatal.  Who gets the lock after a release is not ordered.
 *
 * Mutual exclusion.  X is only ever set by 0 -> X, so not while a shared holder exists or X is
 * already set; R only grows from a value without X.  Both are single compare-and-swaps.
 *
 * Lost wake-ups.  A thread sleeps only in wait(S, s) for an s that it read with WAIT set, or in
 * which it set WAIT itself, and that is a held value.  A sleeper can make progress only once
 * WAIT has been cleared: a writer needs S == 0; a reader needs neither X nor WAIT, and X is
 * only cleared together with WAIT.  WAIT is cleared in exactly two places, the two releases
 * that free the lock, and each is followed by wake ALL by the same thread.  So let S be on the
 * queue from time t, when the kernel read s with WAIT set, and let t1 > t be the first later
 * moment at which WAIT is cleared (if there is none, the lock was never freed and S has nothing
 * to wake for).  The thread that cleared it calls wake-all after t1, S is still on the queue
 * (nothing else removes it), and S wakes.  A thread whose wait comes after t1 either finds S
 * changed and does not sleep, or finds WAIT set again by a later thread on a lock that is again
 * held, and the argument applies to the next clearing.  Waking everybody means no choice of
 * "which one" has to be argued; a woken thread that cannot get the lock sets WAIT again and
 * sleeps again.
 */

#define SRW_X          0x1u
#define SRW_WAIT       0x2u
#define SRW_ONE        0x4u                 /* one shared holder */
#define SRW_SHARED_MAX 0xFFFFFFFCu          /* S with the largest count: no room for another holder */
#define SRW_SPIN       256                  /* looks at a held lock before sleeping on it */

static __attribute__((noinline, cold)) _Noreturn void srw_misaligned(const void *l) {
    lock_fatal(NULL, "the SRW lock at %p is not 4-byte aligned, so its word cannot be waited on", l);
}

/* Windows does not check the alignment; see the same check in rt_cs_init.  An SRW lock needs
 * no initialisation call, so the check is made by every operation. */
static inline uint32_t *srw_word(SRWLOCK *l) {
    if (__builtin_expect((uintptr_t)l & 3, 0)) srw_misaligned(l);
    return (uint32_t *)&l->Ptr;
}

void rt_srw_init(SRWLOCK *l) {
    (void)srw_word(l);
    l->Ptr = NULL;
}

static __attribute__((noinline)) void srw_acquire_exclusive_slow(uint32_t *w) {
    unsigned spins = SRW_SPIN;
    for (;;) {
        uint32_t s = ld32(w);
        if (s == 0) {
            if (cas32(w, 0, SRW_X)) return;
            continue;
        }
        if (!(s & SRW_WAIT)) {
            if (spins) { spins--; cpu_relax(); continue; }
            if (!cas32(w, s, s | SRW_WAIT)) continue;   /* S moved: look again before sleeping */
            s |= SRW_WAIT;
        }
        lock_wait(w, s);                                /* s is held and has WAIT set */
    }
}

void rt_srw_acquire_exclusive(SRWLOCK *l) {
    uint32_t *w = srw_word(l);
    if (__builtin_expect(!cas32(w, 0, SRW_X), 0)) srw_acquire_exclusive_slow(w);
}

BOOLEAN rt_srw_try_acquire_exclusive(SRWLOCK *l) {
    return cas32(srw_word(l), 0, SRW_X);
}

void rt_srw_release_exclusive(SRWLOCK *l) {
    uint32_t *w = srw_word(l);
    if (__builtin_expect(cas32(w, SRW_X, 0), 1)) return;
    /* Held exclusively the word is X or X|WAIT, and WAIT can be set but not cleared under the
     * holder, so this loop runs once or twice. */
    for (;;) {
        uint32_t s = ld32(w);
        if (!(s & SRW_X))
            lock_fatal(NULL, "ReleaseSRWLockExclusive on the SRW lock at %p, which is not held exclusively (its "
                       "word is %#x: %s).  Windows raises STATUS_RESOURCE_NOT_OWNED", (void *)l, s,
                       s == 0 ? "free" : "held shared");
        if (cas32(w, s, 0)) {
            if (s & SRW_WAIT) lock_wake(w, true);
            return;
        }
    }
}

/* Add one shared holder if the lock allows it now.  Returns 1 if added, 0 if the caller must
 * wait (held exclusively, or a thread is already waiting); a compare-and-swap lost to another
 * reader is retried, since it is not a reason to wait. */
static inline int srw_try_shared(SRWLOCK *l, uint32_t *w) {
    for (;;) {
        uint32_t s = ld32(w);
        if (s & (SRW_X | SRW_WAIT)) return 0;
        if (__builtin_expect(s >= SRW_SHARED_MAX, 0))
            lock_fatal(NULL, "the SRW lock at %p already has %u shared holders, the most its word can count",
                       (void *)l, s >> 2);
        if (cas32(w, s, s + SRW_ONE)) return 1;
    }
}

static __attribute__((noinline)) void srw_acquire_shared_slow(SRWLOCK *l, uint32_t *w) {
    unsigned spins = SRW_SPIN;
    for (;;) {
        if (srw_try_shared(l, w)) return;
        uint32_t s = ld32(w);
        if (!(s & (SRW_X | SRW_WAIT))) continue;        /* it changed in our favour: try again */
        if (!(s & SRW_WAIT)) {                          /* held exclusively and nobody is waiting yet */
            if (spins) { spins--; cpu_relax(); continue; }
            if (!cas32(w, s, s | SRW_WAIT)) continue;
            s |= SRW_WAIT;
        }
        lock_wait(w, s);                                /* s is held and has WAIT set */
    }
}

void rt_srw_acquire_shared(SRWLOCK *l) {
    uint32_t *w = srw_word(l);
    uint32_t s = ld32(w);
    if (__builtin_expect(!(s & (SRW_X | SRW_WAIT)) && s < SRW_SHARED_MAX && cas32(w, s, s + SRW_ONE), 1)) return;
    srw_acquire_shared_slow(l, w);
}

BOOLEAN rt_srw_try_acquire_shared(SRWLOCK *l) {
    uint32_t *w = srw_word(l);
    return (BOOLEAN)srw_try_shared(l, w);
}

void rt_srw_release_shared(SRWLOCK *l) {
    uint32_t *w = srw_word(l);
    for (;;) {
        uint32_t s = ld32(w);
        if (__builtin_expect((s & SRW_X) || s < SRW_ONE, 0))
            lock_fatal(NULL, "ReleaseSRWLockShared on the SRW lock at %p, which is not held shared (its word is "
                       "%#x: %s).  Windows raises STATUS_RESOURCE_NOT_OWNED", (void *)l, s,
                       (s & SRW_X) ? "held exclusively" : "free");
        /* The last shared holder frees the lock and so clears WAIT in the same step. */
        uint32_t n = s - SRW_ONE < SRW_ONE ? 0 : s - SRW_ONE;
        if (cas32(w, s, n)) {
            if (__builtin_expect(n == 0 && (s & SRW_WAIT), 0)) lock_wake(w, true);
            return;
        }
    }
}

/* ==== CONDITION_VARIABLE =========================================================================== */
/* The 8 bytes are one 64-bit value V:
 *
 *     bits 0..31    Q   the wake sequence (RUNTIME.md 6.4); the word that sleepers wait on
 *     bits 32..63   N   the number of threads inside a Sleep call on this variable
 *
 * N is an addition to the layout of RUNTIME.md, which leaves the upper half unspecified: it
 * lets a wake that has nobody to wake return after one atomic load, without a system call.
 *
 * Sleep, with the caller holding its lock:
 *   1. register: V += 1 << 32, one atomic operation that also returns the value before it.
 *      The low half of that value is q, the sequence this sleeper saw;
 *   2. release the lock;
 *   3. wait(Q, q) until Q != q or the timeout passes;
 *   4. unregister: V -= 1 << 32;
 *   5. take the lock again, in every case, as Windows does before SleepConditionVariable
 *      returns; then on timeout set last error ERROR_TIMEOUT and return FALSE.
 * Wake and WakeAll:
 *   1. read V; if N == 0 return;
 *   2. Q += 1 by compare-and-swap on V (so that N is left as it is, and Q wraps without
 *      carrying into N);
 *   3. wake one sleeper on Q, or all of them.
 *
 * Lost wake-ups.  Registration and the wake's step 2 (or the read in step 1 that finds N == 0)
 * are atomic operations on the same value, so one comes first.
 *   - The wake comes first.  Then the thread was not yet sleeping on the variable when the wake
 *     was issued and is rightly not woken by it; on Windows too, a wake only reaches threads
 *     that are already queued.  With the usual discipline this case does not arise for a
 *     thread that needs the wake: it registers while still holding the lock under which it
 *     tested its condition, so a thread that changes the condition under that lock and wakes
 *     afterwards either ran before the test (and the sleeper saw the change) or issues its
 *     wake after the registration.
 *   - Registration comes first.  Then the wake sees N >= 1 (a sleeper only takes its count back
 *     after it has stopped waiting) and advances Q, so the sleeper's q is stale from then on,
 *     and the wake call follows.  By the argument at the top of this file the sleeper is
 *     either on the queue before that wake call or does not go to sleep at all.  WakeAll
 *     therefore wakes every thread registered before it.  Wake wakes one thread from the
 *     queue: if any thread registered before it is still on the queue, some sleeper on the
 *     queue wakes, though it may be one that registered between steps 2 and 3 of the wake.
 *     That is within the Windows contract (the woken thread is unspecified, and such a thread
 *     was sleeping on the variable when the wake call ran), and it cannot strand a waiter that
 *     the discipline above protects: a thread can only register after step 2 with its
 *     condition false if another thread has already consumed the change that the wake
 *     announced.
 *   Because Q is read in the same atomic step that registers the sleeper, a wake issued without
 *   the lock (after a flag is set with no lock held) is also never missed once the sleeper has
 *   registered, as on Windows.
 *
 * Q repeats after 2^32 wakes.  A sleeper would have to be stopped between steps 1 and 3 for
 * exactly that many wakes to miss one.
 *
 * Spurious wake-ups are allowed by Windows and happen here: a wake of one thread also releases
 * every registered thread that has not reached the kernel yet.  Returns from the kernel for no
 * reason are not passed on: the wait is repeated while Q is unchanged.
 */

#define CV_SLEEPER  (UINT64_C(1) << 32)

static __attribute__((noinline, cold)) _Noreturn void cv_misaligned(const void *cv) {
    lock_fatal(NULL, "the condition variable at %p is not 8-byte aligned, so it cannot be updated atomically and "
               "waited on", cv);
}

/* The 64-bit value needs 8-byte alignment for its atomic operations (they must not straddle
 * a 16-byte block) and its low half 4-byte alignment for the wait.  Windows does not check. */
static inline uint64_t *cv_value(CONDITION_VARIABLE *cv) {
    if (__builtin_expect((uintptr_t)cv & 7, 0)) cv_misaligned(cv);
    return (uint64_t *)&cv->Ptr;
}

/* Q: the low half of V on this little-endian machine. */
static inline uint32_t *cv_seq(CONDITION_VARIABLE *cv) { return (uint32_t *)&cv->Ptr; }

void rt_cv_init(CONDITION_VARIABLE *cv) {
    (void)cv_value(cv);
    cv->Ptr = NULL;
}

/* Steps 3 and 4 of Sleep.  Returns true if the sequence moved on, false on timeout. */
static bool cv_block(CONDITION_VARIABLE *cv, uint32_t q, DWORD timeout_ms) {
    uint32_t *w = cv_seq(cv);
    bool woken = true;
    if (timeout_ms == INFINITE) {
        while (ld32(w) == q) lock_wait(w, q);
    } else {
        /* Windows rule: the timeout is in milliseconds and 0 means "do not wait at all".  The
         * wait is not rounded to the 15.6 ms timer tick (RUNTIME.md 6.3 does the same for
         * Sleep); it never returns before the time has passed. */
        uint64_t deadline = lock_now_ns() + (uint64_t)timeout_ms * UINT64_C(1000000);
        while (ld32(w) == q) {
            if (!lock_wait_until(w, q, deadline)) {
                /* A wake that arrived together with the timeout counts as a wake: it has
                 * already been charged to this sleeper by whoever issued it. */
                woken = ld32(w) != q;
                break;
            }
        }
    }
    __atomic_fetch_sub(cv_value(cv), CV_SLEEPER, __ATOMIC_SEQ_CST);
    return woken;
}

BOOL rt_cv_sleep_srw(Ctx *ctx, CONDITION_VARIABLE *cv, SRWLOCK *l, DWORD timeout_ms, ULONG flags) {
    /* Not sure what Windows does with other flag bits (ntdll is believed to fail the call with
     * an invalid-parameter status).  No caller is known to pass any: stop. */
    if (flags & ~(ULONG)CONDITION_VARIABLE_LOCKMODE_SHARED)
        RT_UNIMPLEMENTED(ctx, "SleepConditionVariableSRW with Flags %#x: only CONDITION_VARIABLE_LOCKMODE_SHARED "
                         "(0x1) is known", flags);
    bool shared = (flags & CONDITION_VARIABLE_LOCKMODE_SHARED) != 0;
    uint32_t q = (uint32_t)__atomic_fetch_add(cv_value(cv), CV_SLEEPER, __ATOMIC_SEQ_CST);

    /* Windows rule: the lock must be held by the caller in the mode the flags say, exactly
     * once.  The release is fatal if it is not held in that mode. */
    if (shared) rt_srw_release_shared(l); else rt_srw_release_exclusive(l);
    bool woken = cv_block(cv, q, timeout_ms);
    if (shared) rt_srw_acquire_shared(l); else rt_srw_acquire_exclusive(l);

    if (woken) return TRUE;                 /* last error is left alone on success (RUNTIME.md 3.4) */
    return FAIL(ctx, ERROR_TIMEOUT, FALSE);
}

BOOL rt_cv_sleep_cs(Ctx *ctx, CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD timeout_ms) {
    uint32_t q = (uint32_t)__atomic_fetch_add(cv_value(cv), CV_SLEEPER, __ATOMIC_SEQ_CST);

    /* Windows rule: the critical section must have been entered exactly once by the caller.
     * Windows leaves it once and enters it once, whatever the recursion count, so a section
     * entered twice stays owned during the sleep and whoever should wake us cannot get in.
     * The same is done here, with a line in the log because that hang is hard to find.  A
     * section the caller does not own is fatal in rt_cs_leave. */
    if (cs->RecursionCount > 1 && cs_owner(cs) == cs_self(ctx))
        rt_log(RT_CH_THREAD, RT_LL_WARN, "SleepConditionVariableCS(%p, %p): the critical section was entered %d "
               "times by thread 0x%x; it stays owned while the thread sleeps", (void *)cv, (void *)cs,
               cs->RecursionCount, (uint32_t)(uintptr_t)cs_self(ctx));
    rt_cs_leave(ctx, cs);
    bool woken = cv_block(cv, q, timeout_ms);
    rt_cs_enter(ctx, cs);

    if (woken) return TRUE;
    return FAIL(ctx, ERROR_TIMEOUT, FALSE);
}

static void cv_wake(CONDITION_VARIABLE *cv, bool all) {
    uint64_t *p = cv_value(cv);
    uint64_t v = __atomic_load_n(p, __ATOMIC_SEQ_CST);
    for (;;) {
        if ((v >> 32) == 0) return;                     /* nobody is sleeping: the whole uncontended path */
        uint64_t n = (v & ~UINT64_C(0xFFFFFFFF)) | (uint32_t)((uint32_t)v + 1);
        if (__atomic_compare_exchange_n(p, &v, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) break;
    }
    lock_wake(cv_seq(cv), all);
}

void rt_cv_wake(CONDITION_VARIABLE *cv)     { cv_wake(cv, false); }
void rt_cv_wake_all(CONDITION_VARIABLE *cv) { cv_wake(cv, true); }

/* ==== SLIST_HEADER ================================================================================= */
/* Layout (RUNTIME.md 6.4): the low qword holds the depth in bits 0-15, where QueryDepthSList
 * reads it, and a sequence number in bits 16-63; the high qword is the address of the first
 * entry, 0 when the list is empty.  All-zero is an empty list.  Nothing here blocks.
 *
 * Push, pop and flush each read the two qwords, work out the new header and install it with
 * one 128-bit compare-and-swap, retrying if the header changed.  EVERY successful operation
 * adds one to the sequence.
 *
 * Why the two reads need not be one atomic 128-bit read.  The low qword is read first, at t1,
 * then the high qword at t2; the compare-and-swap succeeds at t4 only if the header then equals
 * exactly the pair read.  Every successful operation changes the sequence, which only grows,
 * so success at t4 means no operation completed between t1 and t4: the header held the same
 * value throughout, and the pair read was a true snapshot of it.  The order is essential and
 * so is reading the low qword before first->Next: read after them, the sequence would vouch
 * only for the time since it was read, and a pointer or a Next from before that would pass.
 *
 * ABA.  Pop also reads first->Next, at t3, after t2.  Another thread may pop that entry, hand
 * it back and push it again while we look, so that the first entry is the same address with a
 * different successor; or the entry's memory may be reused and Next be garbage.  Any of that
 * involves a completed operation after t1, so the compare-and-swap fails and the value read is
 * thrown away.  If it succeeds, the entry was the first of the list from t1 to t4, entries on
 * the list are not written by anyone, and the Next that was read is its true successor.
 * The sequence has 48 bits: it repeats after 2^48 operations.  (Windows advances it on push
 * only, which is enough; advancing it always makes the argument above one line.)
 *
 * Ordering.  Push writes entry->Next before the compare-and-swap that makes the entry
 * reachable, and that operation is a release; pop reads the header with acquire ordering
 * before it follows the pointer.  So a pop that sees the new first entry sees its Next.
 *
 * Windows rules followed.  Push and pop work at the front (last in, first out) and return the
 * previous first entry, NULL for an empty list; flush returns the whole chain and leaves the
 * list empty.  The depth is a 16-bit count that wraps: after 65,536 pushes it reads 0 although
 * the list is not empty, and emptiness is decided by the first-entry pointer, never by the
 * depth.  Header and entries must be 16-byte aligned (MEMORY_ALLOCATION_ALIGNMENT): Windows
 * raises STATUS_DATATYPE_MISALIGNMENT for a misaligned header, and is believed to do so for a
 * misaligned entry too (not sure); both are fatal here, and the 128-bit atomic needs the
 * header alignment anyway.
 *
 * One Windows behaviour is NOT reproduced.  Between reading the header and reading
 * first->Next, another thread can pop that entry and free its memory.  If the memory is
 * unmapped the read faults; Windows recognises a fault at that one instruction and restarts
 * the pop.  Here it is an ordinary crash at slist_next below (this runtime has no fault
 * handler yet; the unit that builds it can resume from that function).
 */

#define SL_DEPTH    UINT64_C(0xFFFF)
#define SL_SEQ_ONE  UINT64_C(0x10000)

static __attribute__((noinline, cold)) _Noreturn void slist_misaligned(const char *what, const void *p) {
    lock_fatal(NULL, "interlocked singly linked list: the %s at %p is not 16-byte aligned (Windows raises "
               "STATUS_DATATYPE_MISALIGNMENT)", what, p);
}

static inline void slist_check(const SLIST_HEADER *h) {
    if (__builtin_expect((uintptr_t)h & 15, 0)) slist_misaligned("list header", h);
}

/* The low qword after one more operation that leaves `depth` entries (taken modulo 65,536). */
static inline uint64_t slist_low(uint64_t lo, uint64_t depth) {
    return ((lo & ~SL_DEPTH) + SL_SEQ_ONE) | (depth & SL_DEPTH);
}

static inline bool slist_cas(SLIST_HEADER *h, uint64_t lo, uint64_t hi, uint64_t new_lo, uint64_t new_hi) {
    unsigned __int128 expected = (unsigned __int128)hi << 64 | lo;
    return x_atomic_cmpxchg128((uint64_t)(uintptr_t)h, &expected, (unsigned __int128)new_hi << 64 | new_lo);
}

/* The read that can touch an entry which has just left the list (see above). */
static __attribute__((noinline)) uint64_t slist_next(uint64_t first) {
    return (uint64_t)(uintptr_t)__atomic_load_n(&((SLIST_ENTRY *)(uintptr_t)first)->Next, __ATOMIC_RELAXED);
}

void rt_slist_init(SLIST_HEADER *h) {
    slist_check(h);
    h->Alignment = 0;
    h->Region = 0;
}

PSLIST_ENTRY rt_slist_push(SLIST_HEADER *h, SLIST_ENTRY *entry) {
    slist_check(h);
    if (__builtin_expect(((uintptr_t)entry & 15) || !entry, 0)) {
        if (!entry) lock_fatal(NULL, "InterlockedPushEntrySList(%p): the entry is NULL", (void *)h);
        slist_misaligned("list entry", entry);
    }
    for (;;) {
        uint64_t lo = __atomic_load_n(&h->Alignment, __ATOMIC_ACQUIRE);
        uint64_t hi = __atomic_load_n(&h->Region, __ATOMIC_ACQUIRE);
        /* The entry is still private to this thread: nobody can see it until the swap succeeds. */
        __atomic_store_n(&entry->Next, (SLIST_ENTRY *)(uintptr_t)hi, __ATOMIC_RELAXED);
        if (slist_cas(h, lo, hi, slist_low(lo, lo + 1), (uint64_t)(uintptr_t)entry))
            return (PSLIST_ENTRY)(uintptr_t)hi;
    }
}

PSLIST_ENTRY rt_slist_pop(SLIST_HEADER *h) {
    slist_check(h);
    for (;;) {
        uint64_t lo = __atomic_load_n(&h->Alignment, __ATOMIC_ACQUIRE);
        uint64_t hi = __atomic_load_n(&h->Region, __ATOMIC_ACQUIRE);
        if (hi == 0) return NULL;                       /* empty at the moment the pointer was read */
        uint64_t next = slist_next(hi);
        if (slist_cas(h, lo, hi, slist_low(lo, lo - 1), next)) return (PSLIST_ENTRY)(uintptr_t)hi;
    }
}

PSLIST_ENTRY rt_slist_flush(SLIST_HEADER *h) {
    slist_check(h);
    for (;;) {
        uint64_t lo = __atomic_load_n(&h->Alignment, __ATOMIC_ACQUIRE);
        uint64_t hi = __atomic_load_n(&h->Region, __ATOMIC_ACQUIRE);
        if (hi == 0) return NULL;                       /* an empty list is left untouched */
        if (slist_cas(h, lo, hi, slist_low(lo, 0), 0)) return (PSLIST_ENTRY)(uintptr_t)hi;
    }
}

USHORT rt_slist_depth(const SLIST_HEADER *h) {
    slist_check(h);
    return (USHORT)__atomic_load_n(&h->Alignment, __ATOMIC_RELAXED);
}
