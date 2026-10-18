/* The four lock structures the game embeds in its objects (docs/RUNTIME.md section 6.4,
 * rt/lock.h): layouts, every state a single thread can reach, every boundary and every fatal
 * case.  The tests that need real contention are in lock_stress.c.
 *
 * Two guest threads on one host thread.  The lock code knows a thread only by the thread id
 * in its thread block, so a second hand-made Ctx with its own block is a second guest thread
 * for everything that does not block: who owns a section, what TryEnter answers, who may
 * leave.
 */
#include <stdlib.h>
#include <time.h>

#include "rt/test.h"
#include "rt/lock.h"

#ifndef CONDITION_VARIABLE_LOCKMODE_SHARED
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1u     /* synchapi.h; not in rt/win/sync.h */
#endif

/* A guest thread made by hand: an RtThread whose Ctx points at a thread block that holds
 * what the lock code reads (the thread id at +0x48) and writes (last error at +0x68). */
typedef struct {
    RtThread t;
    uint8_t  block[RT_TEB_SIZE] __attribute__((aligned(16)));
} FakeThread;

static Ctx *fake_thread(FakeThread *f, uint32_t id) {
    memset(f, 0, sizeof *f);
    uint64_t teb = (uint64_t)(uintptr_t)f->block;
    ST64(teb + RT_TEB_SELF, teb);
    ST64(teb + RT_TEB_PROCESS_ID, RT_PROCESS_ID);
    ST64(teb + RT_TEB_THREAD_ID, id);
    f->t.id = id;
    f->t.teb = teb;
    f->t.ctx.gs_base = teb;
    f->t.ctx.mxcsr = 0x1F80;
    f->t.ctx.host = &f->t;
    strcpy(f->t.name, "fake");
    return &f->t.ctx;
}

static uint64_t addr(const void *p) { return (uint64_t)(uintptr_t)p; }

/* The fields of a critical section, read as guest code reads them: by offset. */
#define CS_DEBUG(cs)   LD64(addr(cs) + 0x00)
#define CS_LOCK(cs)    LD32(addr(cs) + 0x08)
#define CS_RECUR(cs)   LD32(addr(cs) + 0x0C)
#define CS_OWNER(cs)   LD64(addr(cs) + 0x10)
#define CS_WORD(cs)    LD32(addr(cs) + 0x18)        /* the wait word: 0 free, 1 held, 2 contended */
#define CS_WORD_HI(cs) LD32(addr(cs) + 0x1C)
#define CS_SPIN(cs)    LD64(addr(cs) + 0x20)

#define T_CS_FREE(cs) do { \
        T_EQ(CS_DEBUG(cs), UINT64_MAX); T_EQ(CS_LOCK(cs), 0xFFFFFFFFu); T_EQ(CS_RECUR(cs), 0); \
        T_EQ(CS_OWNER(cs), 0); T_EQ(CS_WORD(cs), 0); T_EQ(CS_WORD_HI(cs), 0); } while (0)
#define T_CS_HELD(cs, id, depth) do { \
        T_EQ(CS_DEBUG(cs), UINT64_MAX); T_EQ(CS_LOCK(cs), 0); T_EQ(CS_RECUR(cs), (depth)); \
        T_EQ(CS_OWNER(cs), (id)); T_EQ(CS_WORD(cs), 1); T_EQ(CS_WORD_HI(cs), 0); } while (0)

static bool all_zero(const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) if (((const uint8_t *)p)[i]) return false;
    return true;
}

static double ms_since(uint64_t t0) { return (double)(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0) / 1e6; }

/* ---- CRITICAL_SECTION ----------------------------------------------------------------------- */

TEST(core, lock_cs_init)
{
    T_EQ(sizeof(CRITICAL_SECTION), 40);

    /* Whatever the memory held, an initialised section is: DebugInfo -1, LockCount -1, no
     * owner, no recursion, wait word 0. */
    CRITICAL_SECTION cs;
    memset(&cs, 0xA5, sizeof cs);
    rt_cs_init(&cs, 0);
    T_CS_FREE(&cs);
    T_EQ(CS_SPIN(&cs), 0);
    T_TRUE(cs.DebugInfo == (PVOID)(intptr_t)-1 && cs.LockCount == -1 && cs.OwningThread == NULL &&
           cs.LockSemaphore == NULL);

    /* The spin count keeps its low 24 bits (the upper bits are flags on Windows). */
    static const struct { DWORD in; uint64_t stored; } spins[] = {
        { 1, 1 }, { 4000, 4000 }, { 0x00FFFFFF, 0x00FFFFFF }, { 0x01000000, 0 }, { 0x01000001, 1 },
        { 0x80000FA0, 0xFA0 }, { 0xFFFFFFFF, 0x00FFFFFF },
    };
    for (unsigned i = 0; i < sizeof spins / sizeof *spins; i++) {
        memset(&cs, 0x5A, sizeof cs);
        rt_cs_init(&cs, spins[i].in);
        T_CS_FREE(&cs);
        T_EQ(CS_SPIN(&cs), spins[i].stored);
    }

    /* Initialising again gives a fresh section, also over one that looks held. */
    rt_cs_enter(T_CTX(), &cs);
    rt_cs_init(&cs, 7);
    T_CS_FREE(&cs);
    T_EQ(CS_SPIN(&cs), 7);

    /* A section whose wait word could not be waited on is refused at once, not at the first
     * contended Enter.  4-byte alignment is enough. */
    static uint64_t raw[8];
    for (unsigned off = 1; off < 4; off++)
        T_TRAPS(rt_cs_init((CRITICAL_SECTION *)((char *)raw + off), 0), "is not 4-byte aligned");
    CRITICAL_SECTION *odd = (CRITICAL_SECTION *)((char *)raw + 4);
    rt_cs_init(odd, 0);
    rt_cs_enter(T_CTX(), odd);
    T_CS_HELD(odd, RT_FIRST_THREAD_ID, 1);
    rt_cs_leave(T_CTX(), odd);
    T_CS_FREE(odd);
}

TEST(core, lock_cs_enter_leave)
{
    Ctx *ctx = T_CTX();
    CRITICAL_SECTION cs;
    rt_cs_init(&cs, 4000);

    /* OwningThread is the thread id of the thread block, RecursionCount the depth. */
    rt_set_last_error(ctx, 0x1234);
    rt_cs_enter(ctx, &cs);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);
    T_EQ(CS_SPIN(&cs), 4000);

    /* Recursive: each Enter and each successful TryEnter by the owner adds one, each Leave
     * takes one off, and only the last Leave frees the section. */
    for (unsigned i = 2; i <= 1000; i++) {
        if (i % 3) rt_cs_enter(ctx, &cs);
        else T_REQUIRE(rt_cs_try_enter(ctx, &cs) == TRUE);
        T_REQUIRE(CS_RECUR(&cs) == i && CS_OWNER(&cs) == RT_FIRST_THREAD_ID && CS_WORD(&cs) == 1 && CS_LOCK(&cs) == 0);
    }
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1000);
    for (unsigned i = 999; i >= 1; i--) {
        rt_cs_leave(ctx, &cs);
        T_REQUIRE(CS_RECUR(&cs) == i && CS_OWNER(&cs) == RT_FIRST_THREAD_ID && CS_WORD(&cs) == 1 && CS_LOCK(&cs) == 0);
    }
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);
    rt_cs_leave(ctx, &cs);
    T_CS_FREE(&cs);
    T_EQ(CS_SPIN(&cs), 4000);
    T_EQ(T_LAST_ERROR(), 0x1234);               /* none of this touches last error */

    /* TryEnter on a free section takes it. */
    T_EQ(rt_cs_try_enter(ctx, &cs), TRUE);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);
    rt_cs_leave(ctx, &cs);
    T_CS_FREE(&cs);

    /* The id comes from the thread block at +0x48, where GetCurrentThreadId reads it. */
    FakeThread fake;
    Ctx *other = fake_thread(&fake, 0x2468);
    rt_cs_enter(other, &cs);
    T_CS_HELD(&cs, 0x2468, 1);
    rt_cs_leave(other, &cs);
    T_CS_FREE(&cs);
}

TEST(core, lock_cs_two_threads)
{
    Ctx *a = T_CTX();
    FakeThread fb;
    Ctx *b = fake_thread(&fb, 0x2000);
    CRITICAL_SECTION cs;
    rt_cs_init(&cs, 0);

    /* TryEnter by another thread fails without waiting and changes nothing, last error included. */
    rt_cs_enter(a, &cs);
    rt_set_last_error(b, 0x4321);
    T_EQ(rt_cs_try_enter(b, &cs), FALSE);
    T_EQ(rt_cs_try_enter(b, &cs), FALSE);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);
    T_EQ(rt_get_last_error(b), 0x4321);
    rt_cs_enter(a, &cs);
    T_EQ(rt_cs_try_enter(b, &cs), FALSE);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 2);

    /* Leaving a section the caller does not own is fatal, while another thread holds it ... */
    T_TRAPS(rt_cs_leave(b, &cs), "that the calling thread 0x2000 does not own (OwningThread is 0x1004");
    rt_cs_leave(a, &cs);
    T_EQ(rt_cs_try_enter(b, &cs), FALSE);       /* still held: one Leave of two */
    rt_cs_leave(a, &cs);
    T_CS_FREE(&cs);

    /* ... and when nobody does: one Leave too many. */
    T_TRAPS(rt_cs_leave(a, &cs), "does not own (OwningThread is 0x0");
    T_TRAPS(rt_cs_leave(b, &cs), "does not own");

    /* Ownership passes to whoever takes the free section. */
    T_EQ(rt_cs_try_enter(b, &cs), TRUE);
    T_CS_HELD(&cs, 0x2000, 1);
    T_EQ(rt_cs_try_enter(a, &cs), FALSE);
    T_TRAPS(rt_cs_leave(a, &cs), "that the calling thread 0x1004 does not own (OwningThread is 0x2000");
    rt_cs_leave(b, &cs);
    T_CS_FREE(&cs);
}

TEST(core, lock_cs_misuse)
{
    Ctx *ctx = T_CTX();
    CRITICAL_SECTION cs;

    /* A section must be initialised: an all-zero one (a static never initialised, or one that
     * was deleted) is refused by Enter and TryEnter, and Leave finds no owner. */
    memset(&cs, 0, sizeof cs);
    T_TRAPS(rt_cs_enter(ctx, &cs), "EnterCriticalSection on a critical section");
    T_TRAPS(rt_cs_enter(ctx, &cs), "never initialised, or was deleted (DebugInfo is 0x0");
    T_TRAPS(rt_cs_try_enter(ctx, &cs), "TryEnterCriticalSection on a critical section");
    T_TRAPS(rt_cs_leave(ctx, &cs), "does not own");
    T_TRUE(all_zero(&cs, sizeof cs));

    /* So is stale memory: only the -1 that initialisation stores is accepted. */
    memset(&cs, 0, sizeof cs);
    cs.DebugInfo = (PVOID)(uintptr_t)0x1234;
    T_TRAPS(rt_cs_enter(ctx, &cs), "DebugInfo is 0x1234");
    T_TRAPS(rt_cs_try_enter(ctx, &cs), "DebugInfo is 0x1234");

    /* A thread block with thread id 0 cannot own anything: 0 is "no owner". */
    FakeThread fz;
    Ctx *zero = fake_thread(&fz, 0);
    rt_cs_init(&cs, 0);
    T_TRAPS(rt_cs_enter(zero, &cs), "holds thread id 0");
    T_TRAPS(rt_cs_try_enter(zero, &cs), "holds thread id 0");
    T_TRAPS(rt_cs_leave(zero, &cs), "holds thread id 0");
    T_CS_FREE(&cs);

    /* The recursion count has an upper limit, reached exactly. */
    rt_cs_enter(ctx, &cs);
    cs.RecursionCount = INT32_MAX - 1;
    rt_cs_enter(ctx, &cs);
    T_EQ(cs.RecursionCount, INT32_MAX);
    T_TRAPS(rt_cs_enter(ctx, &cs), "RecursionCount is 2147483647");
    T_TRAPS(rt_cs_try_enter(ctx, &cs), "RecursionCount is 2147483647");
    rt_cs_leave(ctx, &cs);
    T_EQ(cs.RecursionCount, INT32_MAX - 1);
    cs.RecursionCount = 1;
    rt_cs_leave(ctx, &cs);
    T_CS_FREE(&cs);

    /* A structure the guest overwrote while it was owned is reported, not repaired. */
    rt_cs_enter(ctx, &cs);
    cs.RecursionCount = 0;
    T_TRAPS(rt_cs_leave(ctx, &cs), "is owned by the caller but its RecursionCount is 0");
    T_TRAPS(rt_cs_enter(ctx, &cs), "RecursionCount is 0");
    cs.RecursionCount = 1;
    ST32(addr(&cs) + 0x18, 0);
    T_TRAPS(rt_cs_leave(ctx, &cs), "the structure was overwritten");
    ST32(addr(&cs) + 0x18, 7);
    T_TRAPS(rt_cs_leave(ctx, &cs), "was 0x7 while the caller owned it");
}

TEST(core, lock_cs_delete)
{
    Ctx *ctx = T_CTX();
    CRITICAL_SECTION cs;

    /* Delete zeroes all 40 bytes, and says nothing about a section that is free. */
    rt_cs_init(&cs, 0x123456);
    rt_cs_enter(ctx, &cs);
    rt_cs_leave(ctx, &cs);
    rt_cs_delete(&cs);
    T_TRUE(all_zero(&cs, sizeof cs));
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "DeleteCriticalSection"));

    /* A deleted section is as unusable as one never initialised; deleting it again, or
     * deleting one that was never initialised, does nothing. */
    T_TRAPS(rt_cs_enter(ctx, &cs), "never initialised, or was deleted");
    T_TRAPS(rt_cs_try_enter(ctx, &cs), "never initialised, or was deleted");
    rt_cs_delete(&cs);
    T_TRUE(all_zero(&cs, sizeof cs));
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "DeleteCriticalSection"));

    /* It can be initialised and used again. */
    rt_cs_init(&cs, 0);
    T_EQ(rt_cs_try_enter(ctx, &cs), TRUE);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);

    /* Deleting a held section is logged, with the owner, and still zeroes it. */
    rt_cs_enter(ctx, &cs);
    rt_cs_delete(&cs);
    T_TRUE(all_zero(&cs, sizeof cs));
    T_TRUE(T_LOGGED(RT_CH_THREAD, "the section is held by thread 0x1004 (recursion count 2)"));
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "will never wake"));

    /* A held section that threads may be asleep on (wait word 2): the log says they stay asleep. */
    rt_cs_init(&cs, 0);
    rt_cs_enter(ctx, &cs);
    ST32(addr(&cs) + 0x18, 2);
    rt_cs_delete(&cs);
    T_TRUE(all_zero(&cs, sizeof cs));
    T_TRUE(T_LOGGED(RT_CH_THREAD, "threads may be waiting for it and will never wake"));
}

TEST(core, lock_cs_spin_count)
{
    Ctx *ctx = T_CTX();
    CRITICAL_SECTION cs;
    rt_cs_init(&cs, 4000);

    /* SetCriticalSectionSpinCount stores the low 24 bits and returns what was there. */
    T_EQ(rt_cs_set_spin(&cs, 0), 4000);
    T_EQ(CS_SPIN(&cs), 0);
    T_EQ(rt_cs_set_spin(&cs, 0xFFFFFFFF), 0);
    T_EQ(CS_SPIN(&cs), 0x00FFFFFF);
    T_EQ(rt_cs_set_spin(&cs, 0x01000001), 0x00FFFFFF);
    T_EQ(CS_SPIN(&cs), 1);
    T_EQ(rt_cs_set_spin(&cs, 0x00FFFFFF), 1);
    T_EQ(rt_cs_set_spin(&cs, 0x01000000), 0x00FFFFFF);
    T_EQ(CS_SPIN(&cs), 0);
    T_CS_FREE(&cs);

    /* On a held section it changes nothing else. */
    rt_cs_enter(ctx, &cs);
    T_EQ(rt_cs_set_spin(&cs, 123), 0);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);
    T_EQ(CS_SPIN(&cs), 123);
    rt_cs_leave(ctx, &cs);
    T_CS_FREE(&cs);

    /* A section with a spin count behaves the same when it is not contended. */
    rt_cs_init(&cs, 0x00FFFFFF);
    FakeThread fb;
    Ctx *b = fake_thread(&fb, 0x2000);
    rt_cs_enter(ctx, &cs);
    T_EQ(rt_cs_try_enter(b, &cs), FALSE);
    rt_cs_leave(ctx, &cs);
    T_EQ(rt_cs_try_enter(b, &cs), TRUE);
    rt_cs_leave(b, &cs);
    T_CS_FREE(&cs);
}

/* ---- SRWLOCK ---------------------------------------------------------------------------------- */

#define SRW_WORD(l)  LD32(addr(l))
#define SRW_ALL(l)   LD64(addr(l))

TEST(core, lock_srw_states)
{
    T_EQ(sizeof(SRWLOCK), 8);

    /* All-zero is an unlocked lock: no initialisation call is needed (SRWLOCK_INIT). */
    SRWLOCK l = { 0 };
    rt_srw_acquire_exclusive(&l);
    T_EQ(SRW_ALL(&l), 1);                       /* bit 0: exclusive */
    T_EQ(rt_srw_try_acquire_exclusive(&l), FALSE);
    T_EQ(rt_srw_try_acquire_shared(&l), FALSE);
    T_EQ(SRW_ALL(&l), 1);
    rt_srw_release_exclusive(&l);
    T_EQ(SRW_ALL(&l), 0);

    /* Shared holders are counted from bit 2 up, and exclude an exclusive holder. */
    rt_srw_acquire_shared(&l);
    T_EQ(SRW_ALL(&l), 4);
    rt_srw_acquire_shared(&l);
    T_EQ(SRW_ALL(&l), 8);
    T_EQ(rt_srw_try_acquire_shared(&l), TRUE);
    T_EQ(SRW_ALL(&l), 12);
    T_EQ(rt_srw_try_acquire_exclusive(&l), FALSE);
    T_EQ(SRW_ALL(&l), 12);
    rt_srw_release_shared(&l);
    rt_srw_release_shared(&l);
    T_EQ(SRW_ALL(&l), 4);
    T_EQ(rt_srw_try_acquire_exclusive(&l), FALSE);
    rt_srw_release_shared(&l);
    T_EQ(SRW_ALL(&l), 0);

    T_EQ(rt_srw_try_acquire_exclusive(&l), TRUE);
    T_EQ(SRW_ALL(&l), 1);
    rt_srw_release_exclusive(&l);
    T_EQ(rt_srw_try_acquire_shared(&l), TRUE);
    T_EQ(SRW_ALL(&l), 4);
    rt_srw_release_shared(&l);
    T_EQ(SRW_ALL(&l), 0);

    /* Many holders, and back. */
    for (unsigned i = 1; i <= 100000; i++) {
        if (i & 1) rt_srw_acquire_shared(&l);
        else T_REQUIRE(rt_srw_try_acquire_shared(&l) == TRUE);
    }
    T_EQ(SRW_ALL(&l), 400000);
    for (unsigned i = 0; i < 100000; i++) rt_srw_release_shared(&l);
    T_EQ(SRW_ALL(&l), 0);

    /* InitializeSRWLock zeroes the 8 bytes. */
    memset(&l, 0xA5, sizeof l);
    rt_srw_init(&l);
    T_EQ(SRW_ALL(&l), 0);
    T_TRUE(l.Ptr == NULL);

    /* Only the low 32 bits are the lock: the operations leave the upper half alone. */
    l.Ptr = (PVOID)(uintptr_t)UINT64_C(0xDEADBEEF00000000);
    rt_srw_acquire_exclusive(&l);
    T_EQ(SRW_ALL(&l), UINT64_C(0xDEADBEEF00000001));
    rt_srw_release_exclusive(&l);
    rt_srw_acquire_shared(&l);
    T_EQ(SRW_ALL(&l), UINT64_C(0xDEADBEEF00000004));
    rt_srw_release_shared(&l);
    T_EQ(SRW_ALL(&l), UINT64_C(0xDEADBEEF00000000));
}

TEST(core, lock_srw_misuse)
{
    SRWLOCK l = { 0 };

    /* Releasing a lock that is not held in that mode (Windows: STATUS_RESOURCE_NOT_OWNED). */
    T_TRAPS(rt_srw_release_exclusive(&l), "which is not held exclusively (its word is 0: free)");
    T_TRAPS(rt_srw_release_shared(&l), "which is not held shared (its word is 0: free)");
    rt_srw_acquire_shared(&l);
    T_TRAPS(rt_srw_release_exclusive(&l), "which is not held exclusively (its word is 0x4: held shared)");
    rt_srw_release_shared(&l);
    T_TRAPS(rt_srw_release_shared(&l), "which is not held shared");
    rt_srw_acquire_exclusive(&l);
    T_TRAPS(rt_srw_release_shared(&l), "which is not held shared (its word is 0x1: held exclusively)");
    rt_srw_release_exclusive(&l);
    T_TRAPS(rt_srw_release_exclusive(&l), "which is not held exclusively");
    T_EQ(SRW_ALL(&l), 0);

    /* The shared count has 30 bits.  The last value fits; one more is refused loudly. */
    ST32(addr(&l), 0xFFFFFFF8);
    T_EQ(rt_srw_try_acquire_shared(&l), TRUE);
    T_EQ(SRW_WORD(&l), 0xFFFFFFFC);
    T_TRAPS(rt_srw_acquire_shared(&l), "already has 1073741823 shared holders");
    T_TRAPS(rt_srw_try_acquire_shared(&l), "already has 1073741823 shared holders");
    rt_srw_release_shared(&l);
    T_EQ(SRW_WORD(&l), 0xFFFFFFF8);
    ST32(addr(&l), 0xFFFFFFF8);
    rt_srw_acquire_shared(&l);
    T_EQ(SRW_WORD(&l), 0xFFFFFFFC);
    ST32(addr(&l), 0);

    /* A lock whose word could not be waited on is refused by every operation. */
    static uint64_t raw[4];
    for (unsigned off = 1; off < 4; off++) {
        SRWLOCK *bad = (SRWLOCK *)((char *)raw + off);
        T_TRAPS(rt_srw_init(bad), "is not 4-byte aligned");
        T_TRAPS(rt_srw_acquire_exclusive(bad), "is not 4-byte aligned");
        T_TRAPS(rt_srw_release_exclusive(bad), "is not 4-byte aligned");
        T_TRAPS(rt_srw_acquire_shared(bad), "is not 4-byte aligned");
        T_TRAPS(rt_srw_release_shared(bad), "is not 4-byte aligned");
        T_TRAPS(rt_srw_try_acquire_exclusive(bad), "is not 4-byte aligned");
        T_TRAPS(rt_srw_try_acquire_shared(bad), "is not 4-byte aligned");
    }
    SRWLOCK *odd = (SRWLOCK *)((char *)raw + 4);        /* 4-byte alignment is enough */
    rt_srw_init(odd);
    rt_srw_acquire_exclusive(odd);
    T_EQ(SRW_WORD(odd), 1);
    rt_srw_release_exclusive(odd);
    T_EQ(rt_srw_try_acquire_shared(odd), TRUE);
    rt_srw_release_shared(odd);
    T_EQ(SRW_WORD(odd), 0);
}

TEST(core, lock_srw_waiters_bit)
{
    /* Bit 1 says a thread may be asleep on the lock.  Its rules, shown on words written by
     * hand (lock_stress.c gets there with real sleepers). */
    SRWLOCK l = { 0 };

    /* Held shared with a waiter: a new reader does not overtake the waiter, as on Windows. */
    ST32(addr(&l), 4 | 2);
    T_EQ(rt_srw_try_acquire_shared(&l), FALSE);
    T_EQ(rt_srw_try_acquire_exclusive(&l), FALSE);
    T_EQ(SRW_WORD(&l), 4 | 2);
    /* The last shared holder frees the lock and clears the bit in the same step. */
    rt_srw_release_shared(&l);
    T_EQ(SRW_WORD(&l), 0);

    /* A shared holder that is not the last keeps the bit. */
    ST32(addr(&l), 12 | 2);
    rt_srw_release_shared(&l);
    T_EQ(SRW_WORD(&l), 8 | 2);
    rt_srw_release_shared(&l);
    T_EQ(SRW_WORD(&l), 4 | 2);
    rt_srw_release_shared(&l);
    T_EQ(SRW_WORD(&l), 0);

    /* Held exclusively with a waiter. */
    ST32(addr(&l), 1 | 2);
    T_EQ(rt_srw_try_acquire_shared(&l), FALSE);
    T_EQ(rt_srw_try_acquire_exclusive(&l), FALSE);
    T_EQ(SRW_WORD(&l), 1 | 2);
    rt_srw_release_exclusive(&l);
    T_EQ(SRW_WORD(&l), 0);
    T_EQ(rt_srw_try_acquire_exclusive(&l), TRUE);
    rt_srw_release_exclusive(&l);
}

/* ---- CONDITION_VARIABLE ----------------------------------------------------------------------- */

#define CV_ALL(cv)       LD64(addr(cv))
#define CV_SEQ(cv)       LD32(addr(cv))             /* the wake sequence */
#define CV_SLEEPERS(cv)  LD32(addr(cv) + 4)         /* threads inside a Sleep call */

TEST(core, lock_cv_no_sleepers)
{
    T_EQ(sizeof(CONDITION_VARIABLE), 8);

    /* All-zero is valid, and waking a variable nobody sleeps on changes nothing. */
    CONDITION_VARIABLE cv = { 0 };
    rt_cv_wake(&cv);
    rt_cv_wake_all(&cv);
    T_EQ(CV_ALL(&cv), 0);

    memset(&cv, 0xA5, sizeof cv);
    rt_cv_init(&cv);
    T_EQ(CV_ALL(&cv), 0);
    T_TRUE(cv.Ptr == NULL);

    /* A sequence left by earlier wakes is kept: only a wake that finds sleepers advances it. */
    ST32(addr(&cv), 0xFFFFFFFF);
    rt_cv_wake(&cv);
    rt_cv_wake_all(&cv);
    T_EQ(CV_ALL(&cv), 0xFFFFFFFF);

    static uint64_t raw[4];
    for (unsigned off = 1; off < 8; off++) {
        CONDITION_VARIABLE *bad = (CONDITION_VARIABLE *)((char *)raw + off);
        T_TRAPS(rt_cv_init(bad), "is not 8-byte aligned");
        T_TRAPS(rt_cv_wake(bad), "is not 8-byte aligned");
        T_TRAPS(rt_cv_wake_all(bad), "is not 8-byte aligned");
    }
    SRWLOCK l = { 0 };
    CRITICAL_SECTION cs;
    rt_cs_init(&cs, 0);
    CONDITION_VARIABLE *bad = (CONDITION_VARIABLE *)((char *)raw + 4);
    T_TRAPS(rt_cv_sleep_srw(T_CTX(), bad, &l, 0, 0), "is not 8-byte aligned");
    T_TRAPS(rt_cv_sleep_cs(T_CTX(), bad, &cs, 0), "is not 8-byte aligned");
}

TEST(core, lock_cv_timeout_srw)
{
    Ctx *ctx = T_CTX();
    CONDITION_VARIABLE cv = { 0 };
    SRWLOCK l = { 0 };

    /* Timeout 0: release, do not wait, take the lock again, FALSE with ERROR_TIMEOUT. */
    rt_srw_acquire_exclusive(&l);
    rt_set_last_error(ctx, 0x1234);
    T_EQ(rt_cv_sleep_srw(ctx, &cv, &l, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_EQ(SRW_ALL(&l), 1);                       /* held exclusively again */
    T_EQ(CV_ALL(&cv), 0);                       /* no sleeper left registered, sequence untouched */

    /* A real timeout: not before the time has passed.  (The upper bound is only there to
     * catch a wrong unit; the machine may be busy.) */
    static const DWORD waits[] = { 1, 20, 40 };
    for (unsigned i = 0; i < sizeof waits / sizeof *waits; i++) {
        rt_set_last_error(ctx, 0x1234);
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        T_EQ(rt_cv_sleep_srw(ctx, &cv, &l, waits[i], 0), FALSE);
        double ms = ms_since(t0);
        T_TRUE(ms >= (double)waits[i]);
        T_TRUE(ms < 10000.0);
        T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
        T_EQ(SRW_ALL(&l), 1);
        T_EQ(CV_ALL(&cv), 0);
    }
    rt_srw_release_exclusive(&l);

    /* Shared mode: the lock is released shared and taken shared again. */
    rt_srw_acquire_shared(&l);
    rt_set_last_error(ctx, 0x1234);
    T_EQ(rt_cv_sleep_srw(ctx, &cv, &l, 0, CONDITION_VARIABLE_LOCKMODE_SHARED), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_EQ(SRW_ALL(&l), 4);
    rt_srw_acquire_shared(&l);                  /* another holder is not disturbed */
    T_EQ(rt_cv_sleep_srw(ctx, &cv, &l, 15, CONDITION_VARIABLE_LOCKMODE_SHARED), FALSE);
    T_EQ(SRW_ALL(&l), 8);
    rt_srw_release_shared(&l);
    rt_srw_release_shared(&l);
    T_EQ(SRW_ALL(&l), 0);
    T_EQ(CV_ALL(&cv), 0);

    /* The lock must be held in the mode the flags say. */
    T_TRAPS(rt_cv_sleep_srw(ctx, &cv, &l, 0, 0), "which is not held exclusively (its word is 0: free)");
    T_TRAPS(rt_cv_sleep_srw(ctx, &cv, &l, 0, CONDITION_VARIABLE_LOCKMODE_SHARED), "which is not held shared");
    rt_srw_acquire_exclusive(&l);
    T_TRAPS(rt_cv_sleep_srw(ctx, &cv, &l, 0, CONDITION_VARIABLE_LOCKMODE_SHARED),
            "which is not held shared (its word is 0x1: held exclusively)");
    rt_srw_release_exclusive(&l);
    rt_srw_acquire_shared(&l);
    T_TRAPS(rt_cv_sleep_srw(ctx, &cv, &l, 0, 0), "which is not held exclusively (its word is 0x4: held shared)");
    rt_srw_release_shared(&l);

    /* Flag bits other than LOCKMODE_SHARED are not known: fatal, naming the value. */
    rt_srw_acquire_exclusive(&l);
    T_TRAPS(rt_cv_sleep_srw(ctx, &cv, &l, 0, 2), "SleepConditionVariableSRW with Flags 0x2");
    T_TRAPS(rt_cv_sleep_srw(ctx, &cv, &l, 0, 3), "SleepConditionVariableSRW with Flags 0x3");
    T_TRAPS(rt_cv_sleep_srw(ctx, &cv, &l, 0, 0x80000000), "SleepConditionVariableSRW with Flags 0x80000000");
    rt_srw_release_exclusive(&l);
    T_EQ(SRW_ALL(&l), 0);
    T_EQ(CV_ALL(&cv), 0);
}

TEST(core, lock_cv_timeout_cs)
{
    Ctx *ctx = T_CTX();
    CONDITION_VARIABLE cv = { 0 };
    CRITICAL_SECTION cs;
    rt_cs_init(&cs, 0);

    rt_cs_enter(ctx, &cs);
    rt_set_last_error(ctx, 0x1234);
    T_EQ(rt_cv_sleep_cs(ctx, &cv, &cs, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);
    T_EQ(CV_ALL(&cv), 0);

    rt_set_last_error(ctx, 0x1234);
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    T_EQ(rt_cv_sleep_cs(ctx, &cv, &cs, 25), FALSE);
    double ms = ms_since(t0);
    T_TRUE(ms >= 25.0 && ms < 10000.0);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 1);
    T_EQ(CV_ALL(&cv), 0);
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "SleepConditionVariableCS"));

    /* Windows leaves the section once and enters it once.  Entered twice, it stays owned
     * during the sleep (nobody could get in to wake us); the count is the same afterwards,
     * and the log says so. */
    rt_cs_enter(ctx, &cs);
    T_EQ(rt_cv_sleep_cs(ctx, &cv, &cs, 5), FALSE);
    T_CS_HELD(&cs, RT_FIRST_THREAD_ID, 2);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "the critical section was entered 2 times by thread 0x1004"));
    rt_cs_leave(ctx, &cs);
    rt_cs_leave(ctx, &cs);
    T_CS_FREE(&cs);

    /* The caller must own the section. */
    T_TRAPS(rt_cv_sleep_cs(ctx, &cv, &cs, 0), "does not own");
    FakeThread fb;
    Ctx *b = fake_thread(&fb, 0x2000);
    rt_cs_enter(b, &cs);
    T_TRAPS(rt_cv_sleep_cs(ctx, &cv, &cs, 0), "that the calling thread 0x1004 does not own (OwningThread is 0x2000");
    /* The other thread's last error is its own. */
    rt_set_last_error(ctx, 0x1234);
    T_EQ(rt_cv_sleep_cs(b, &cv, &cs, 0), FALSE);
    T_EQ(rt_get_last_error(b), ERROR_TIMEOUT);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_CS_HELD(&cs, 0x2000, 1);
    rt_cs_leave(b, &cs);

    /* A section that was never initialised. */
    CRITICAL_SECTION zero;
    memset(&zero, 0, sizeof zero);
    T_TRAPS(rt_cv_sleep_cs(ctx, &cv, &zero, 0), "does not own");
    T_EQ(CV_ALL(&cv), 0);
}

/* ---- SLIST_HEADER ------------------------------------------------------------------------------- */

#define SL_LOW(h)    LD64(addr(h))
#define SL_DEPTH(h)  LD16(addr(h))                  /* where inlined QueryDepthSList reads it */
#define SL_SEQ(h)    (LD64(addr(h)) >> 16)
#define SL_FIRST(h)  LD64(addr(h) + 8)

TEST(core, lock_slist_basics)
{
    T_EQ(sizeof(SLIST_HEADER), 16);
    T_EQ(_Alignof(SLIST_HEADER), 16);
    T_EQ(sizeof(SLIST_ENTRY), 16);

    SLIST_HEADER h;
    memset(&h, 0xA5, sizeof h);
    rt_slist_init(&h);
    T_EQ(SL_LOW(&h), 0);
    T_EQ(SL_FIRST(&h), 0);
    T_EQ(rt_slist_depth(&h), 0);

    /* An empty list: pop and flush return NULL and leave the header exactly as it was. */
    T_TRUE(rt_slist_pop(&h) == NULL);
    T_TRUE(rt_slist_flush(&h) == NULL);
    T_EQ(SL_LOW(&h), 0);
    T_EQ(SL_FIRST(&h), 0);

    /* Push puts the entry in front and returns the previous first entry. */
    SLIST_ENTRY e[4];
    memset(e, 0xEE, sizeof e);
    T_TRUE(rt_slist_push(&h, &e[0]) == NULL);
    T_EQ(SL_FIRST(&h), addr(&e[0]));
    T_TRUE(e[0].Next == NULL);
    T_EQ(SL_DEPTH(&h), 1);
    T_EQ(rt_slist_depth(&h), 1);
    T_TRUE(rt_slist_push(&h, &e[1]) == &e[0]);
    T_EQ(SL_FIRST(&h), addr(&e[1]));
    T_TRUE(e[1].Next == &e[0]);
    T_EQ(rt_slist_depth(&h), 2);
    T_TRUE(rt_slist_push(&h, &e[2]) == &e[1]);
    T_EQ(rt_slist_depth(&h), 3);
    T_EQ(SL_DEPTH(&h), 3);

    /* Pop takes from the front: last in, first out. */
    T_TRUE(rt_slist_pop(&h) == &e[2]);
    T_EQ(SL_FIRST(&h), addr(&e[1]));
    T_EQ(rt_slist_depth(&h), 2);
    T_TRUE(rt_slist_pop(&h) == &e[1]);
    T_TRUE(rt_slist_pop(&h) == &e[0]);
    T_EQ(SL_FIRST(&h), 0);
    T_EQ(rt_slist_depth(&h), 0);
    T_TRUE(rt_slist_pop(&h) == NULL);
    T_EQ(rt_slist_depth(&h), 0);

    /* Flush hands back the whole chain, intact, and leaves the list empty and usable. */
    for (unsigned i = 0; i < 4; i++) rt_slist_push(&h, &e[i]);
    T_EQ(rt_slist_depth(&h), 4);
    PSLIST_ENTRY chain = rt_slist_flush(&h);
    T_TRUE(chain == &e[3] && e[3].Next == &e[2] && e[2].Next == &e[1] && e[1].Next == &e[0] && e[0].Next == NULL);
    T_EQ(SL_FIRST(&h), 0);
    T_EQ(rt_slist_depth(&h), 0);
    T_TRUE(rt_slist_flush(&h) == NULL);
    T_TRUE(rt_slist_pop(&h) == NULL);
    T_TRUE(rt_slist_push(&h, &e[1]) == NULL);
    T_TRUE(rt_slist_pop(&h) == &e[1]);

    /* The sequence above the depth changes with every operation that changes the list (that
     * is what defeats ABA) and with no other. */
    uint64_t seq = SL_SEQ(&h);
    rt_slist_push(&h, &e[0]);
    T_TRUE(SL_SEQ(&h) != seq);
    seq = SL_SEQ(&h);
    rt_slist_push(&h, &e[1]);
    T_TRUE(SL_SEQ(&h) != seq);
    seq = SL_SEQ(&h);
    T_EQ(rt_slist_depth(&h), 2);
    T_EQ(SL_SEQ(&h), seq);
    rt_slist_pop(&h);
    T_TRUE(SL_SEQ(&h) != seq);
    seq = SL_SEQ(&h);
    rt_slist_flush(&h);
    T_TRUE(SL_SEQ(&h) != seq);
    seq = SL_SEQ(&h);
    T_TRUE(rt_slist_pop(&h) == NULL);
    T_TRUE(rt_slist_flush(&h) == NULL);
    T_EQ(SL_SEQ(&h), seq);
    /* Same entry, same depth, pushed again: the header is nevertheless a different value. */
    rt_slist_push(&h, &e[0]);
    uint64_t low = SL_LOW(&h);
    rt_slist_pop(&h);
    rt_slist_push(&h, &e[0]);
    T_EQ(SL_FIRST(&h), addr(&e[0]));
    T_EQ(SL_DEPTH(&h), 1);
    T_TRUE(SL_LOW(&h) != low);

    /* A static all-zero header is an empty list without any call. */
    static SLIST_HEADER zero;
    T_EQ(rt_slist_depth(&zero), 0);
    T_TRUE(rt_slist_pop(&zero) == NULL);
    T_TRUE(rt_slist_push(&zero, &e[2]) == NULL);
    T_TRUE(rt_slist_pop(&zero) == &e[2]);

    /* The sequence wraps in its 48 bits without touching the depth or the pointer. */
    rt_slist_init(&h);
    ST64(addr(&h), UINT64_C(0xFFFFFFFFFFFF0000));
    T_TRUE(rt_slist_push(&h, &e[0]) == NULL);
    T_EQ(SL_LOW(&h), 1);
    T_EQ(SL_FIRST(&h), addr(&e[0]));
    T_TRUE(rt_slist_pop(&h) == &e[0]);
    T_EQ(SL_LOW(&h), 0x10000);
}

TEST(core, lock_slist_depth_wraps)
{
    /* The depth is 16 bits and wraps, as on Windows: after 65,536 pushes it reads 0 on a list
     * that is not empty.  Emptiness is decided by the first-entry pointer. */
    enum { N = 65536 + 3 };
    SLIST_ENTRY *e = NULL;
    T_REQUIRE(posix_memalign((void **)&e, 16, N * sizeof *e) == 0);
    SLIST_HEADER h;
    rt_slist_init(&h);

    bool ok = true;
    for (unsigned i = 0; i < N; i++) {
        PSLIST_ENTRY prev = rt_slist_push(&h, &e[i]);
        ok = ok && prev == (i ? &e[i - 1] : NULL) && rt_slist_depth(&h) == (USHORT)(i + 1);
        if (i == 65534) T_EQ(rt_slist_depth(&h), 65535);
        if (i == 65535) { T_EQ(rt_slist_depth(&h), 0); T_EQ(SL_FIRST(&h), addr(&e[65535])); }
        if (i == 65536) T_EQ(rt_slist_depth(&h), 1);
    }
    T_TRUE(ok);
    T_EQ(rt_slist_depth(&h), 3);

    /* Every entry comes back, in reverse order, the depth counting down through the wrap. */
    ok = true;
    for (unsigned i = N; i-- > 0; ) {
        PSLIST_ENTRY got = rt_slist_pop(&h);
        ok = ok && got == &e[i] && rt_slist_depth(&h) == (USHORT)i;
        if (i == 65536) { T_EQ(rt_slist_depth(&h), 0); T_TRUE(SL_FIRST(&h) != 0); }
        if (i == 65535) T_EQ(rt_slist_depth(&h), 65535);
    }
    T_TRUE(ok);
    T_TRUE(rt_slist_pop(&h) == NULL);
    T_EQ(SL_FIRST(&h), 0);
    T_EQ(rt_slist_depth(&h), 0);

    /* Flush of a list longer than the depth can count: the whole chain, and depth 0. */
    for (unsigned i = 0; i < N; i++) rt_slist_push(&h, &e[i]);
    PSLIST_ENTRY chain = rt_slist_flush(&h);
    T_EQ(rt_slist_depth(&h), 0);
    T_EQ(SL_FIRST(&h), 0);
    unsigned n = 0;
    for (PSLIST_ENTRY p = chain; p && n <= N; p = p->Next) n++;
    T_EQ(n, N);
    free(e);
}

TEST(core, lock_slist_misuse)
{
    /* Header and entries must be 16-byte aligned (Windows: STATUS_DATATYPE_MISALIGNMENT). */
    static SLIST_HEADER h;
    static SLIST_ENTRY e[2];
    static const unsigned offs[] = { 1, 4, 8, 15 };
    for (unsigned i = 0; i < sizeof offs / sizeof *offs; i++) {
        SLIST_HEADER *bad = (SLIST_HEADER *)((char *)&h + offs[i]);
        T_TRAPS(rt_slist_init(bad), "the list header at");
        T_TRAPS(rt_slist_push(bad, &e[0]), "is not 16-byte aligned");
        T_TRAPS(rt_slist_pop(bad), "is not 16-byte aligned");
        T_TRAPS(rt_slist_flush(bad), "is not 16-byte aligned");
        T_TRAPS(rt_slist_depth(bad), "is not 16-byte aligned");
        T_TRAPS(rt_slist_push(&h, (SLIST_ENTRY *)((char *)&e[0] + offs[i])), "the list entry at");
    }
    T_TRAPS(rt_slist_push(&h, NULL), "the entry is NULL");
    T_EQ(SL_LOW(&h), 0);
    T_EQ(SL_FIRST(&h), 0);
}
