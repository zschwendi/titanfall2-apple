/* The lock structures under real contention (docs/RUNTIME.md section 6.4, rt/lock.h).
 *
 * The runtime cannot create guest threads yet, so each contender is a host pthread that owns
 * a hand-made guest thread: an RtThread whose Ctx points at its own thread block, with a
 * thread id at +0x48 (what the critical section stores in OwningThread) and last error at
 * +0x68.  These threads do not run in the x86 ordering mode, and neither does the main
 * thread of a test process, so everything here runs under arm64's weak ordering: the data
 * each lock protects is deliberately plain memory, read and written without atomics.
 *
 * Two kinds of test.  The "blocking" ones are deterministic: the main thread holds a lock,
 * waits until the others are provably queued behind it (the structure says so), checks that
 * none got in, releases, and requires that every one of them then gets through.  The
 * "stress" ones run many operations and check invariants that a race, a lost wake-up or a
 * lost list node would break.  A lost wake-up shows as a thread that never finishes: every
 * test waits for its threads with a deadline and fails if one is still asleep.
 *
 * Only the main thread calls the T_ checks (the harness's counters are not thread safe):
 * workers record their first failed check and the main thread reports it.
 */
#include <mach/mach.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/lock.h"

#ifndef CONDITION_VARIABLE_LOCKMODE_SHARED
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1u     /* synchapi.h; not in rt/win/sync.h */
#endif

/* ---- worker threads ----------------------------------------------------------------------------- */

typedef struct Worker Worker;
struct Worker {
    RtThread    t;                  /* the hand-made guest thread; t.ctx is its Ctx */
    uint8_t     block[RT_TEB_SIZE] __attribute__((aligned(16)));   /* its thread block */
    pthread_t   host;
    void      (*fn)(Worker *);
    void       *arg;                /* the test's shared state */
    unsigned    index;              /* 0, 1, 2 ... within its group */
    uint32_t    id;                 /* guest thread id */
    int         done;               /* set when fn has returned */
    unsigned    errors;             /* failed W_CHECKs, and the first of them */
    int         line;
    const char *what;
    uint64_t    n[6];               /* tallies, meaning per test */
    uint64_t    rng;
};

#define W_CTX(w)  (&(w)->t.ctx)
#define W_CHECK(w, cond) do { if (!(cond) && !(w)->errors++) { (w)->line = __LINE__; (w)->what = #cond; } } while (0)

/* How long a test waits for threads that should finish before it calls them lost, in
 * nanoseconds.  Generous, because the machine may be very busy; a build that wants failures
 * reported quickly can define it smaller. */
#ifndef LOCK_TEST_DEADLINE_NS
#define LOCK_TEST_DEADLINE_NS UINT64_C(40000000000)
#endif

static int      gate;               /* workers start their work when this is set */
static uint32_t next_id = 0x2000;   /* guest thread ids: multiples of 4, never reused */

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static uint64_t addr(const void *p) { return (uint64_t)(uintptr_t)p; }
static int      aload(const int *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void     astore(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }

static uint64_t rnd(Worker *w) {
    w->rng ^= w->rng << 13;
    w->rng ^= w->rng >> 7;
    w->rng ^= w->rng << 17;
    return w->rng;
}

static void *worker_main(void *p) {
    Worker *w = p;
    while (!aload(&gate)) sched_yield();
    w->fn(w);
    astore(&w->done, 1);
    return NULL;
}

/* Start n workers running fn(arg).  They wait for the gate. */
static void workers_start(Worker *w, unsigned n, void (*fn)(Worker *), void *arg) {
    for (unsigned i = 0; i < n; i++) {
        memset(&w[i], 0, sizeof w[i]);
        uint64_t teb = addr(w[i].block);
        w[i].id = next_id;
        next_id += 4;
        ST64(teb + RT_TEB_SELF, teb);
        ST64(teb + RT_TEB_PROCESS_ID, RT_PROCESS_ID);
        ST64(teb + RT_TEB_THREAD_ID, w[i].id);
        w[i].t.id = w[i].id;
        w[i].t.teb = teb;
        w[i].t.ctx.gs_base = teb;
        w[i].t.ctx.mxcsr = 0x1F80;
        w[i].t.ctx.host = &w[i].t;
        snprintf(w[i].t.name, sizeof w[i].t.name, "worker %u", i);
        w[i].fn = fn;
        w[i].arg = arg;
        w[i].index = i;
        w[i].rng = 0x9E3779B97F4A7C15ull * (w[i].id + 1);
        if (pthread_create(&w[i].host, NULL, worker_main, &w[i])) { perror("pthread_create"); abort(); }
    }
}

/* Something that changes whenever any worker's tally does.  Read while the workers run and
 * without synchronisation, on purpose: only "the same as last time or not" is used. */
__attribute__((noinline, no_sanitize("thread")))
static uint64_t progress(const Worker *w, unsigned n) {
    uint64_t h = 0;
    for (unsigned i = 0; i < n; i++)
        for (unsigned k = 0; k < 6; k++) h = h * UINT64_C(0x100000001B3) + w[i].n[k];
    return h;
}

/* Wait for the workers and report what they found.  Threads that have not finished are lost
 * (a lost wake-up or a deadlock) when NO thread's tally has moved for the whole deadline:
 * how long a scenario takes says nothing, a busy machine can make it take any time.  Lost
 * threads end the test at once, as failed: they still use the scenario's static state, and
 * whatever ran next on it would fail for reasons that mean nothing.  So this returns true or
 * not at all. */
static bool workers_finish(Worker *w, unsigned n, const char *what) {
    uint64_t seen = progress(w, n), deadline = now_ns() + LOCK_TEST_DEADLINE_NS;
    for (;;) {
        unsigned done = 0;
        for (unsigned i = 0; i < n; i++) done += aload(&w[i].done) != 0;
        if (done == n) break;
        uint64_t p = progress(w, n);
        if (p != seen) {
            seen = p;
            deadline = now_ns() + LOCK_TEST_DEADLINE_NS;
        } else if (now_ns() > deadline) {
            rt_test_fail(__FILE__, __LINE__, "%s: %u of %u threads never finished and none has done anything for "
                         "%llu s: a lost wake-up or a deadlock; the test ends here", what, n - done, n,
                         (unsigned long long)(LOCK_TEST_DEADLINE_NS / UINT64_C(1000000000)));
            fflush(stdout);
            fflush(stderr);
            _exit(1);
        }
        usleep(500);
    }
    for (unsigned i = 0; i < n; i++) {
        pthread_join(w[i].host, NULL);
        if (w[i].errors)
            rt_test_fail(__FILE__, w[i].line, "%s: thread %u failed %u check(s), the first: %s", what, i,
                         w[i].errors, w[i].what);
    }
    T_TRUE(n > 0);
    return true;
}

static uint64_t tally(const Worker *w, unsigned n, unsigned k) {
    uint64_t s = 0;
    for (unsigned i = 0; i < n; i++) s += w[i].n[k];
    return s;
}

/* True once `expr` holds; false if it does not within half that time. */
#define EVENTUALLY(expr) ({ \
        bool ok_ = false; \
        for (uint64_t t0_ = now_ns(); !(ok_ = (expr)) && now_ns() - t0_ < LOCK_TEST_DEADLINE_NS / 2; ) usleep(200); \
        ok_; })

/* Is the worker's host thread blocked in the kernel?  (Not merely descheduled: a thread that
 * is runnable but has no processor reports "running".)  This is how a test knows that a
 * thread which went to wait for a lock or a condition variable has really gone to sleep. */
static bool worker_blocked(const Worker *w) {
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(pthread_mach_thread_np(w->host), THREAD_BASIC_INFO, (thread_info_t)&info, &count) != KERN_SUCCESS)
        return false;
    return info.run_state == TH_STATE_WAITING;
}

static bool all_blocked(const Worker *w, unsigned n) {
    for (unsigned i = 0; i < n; i++) if (!worker_blocked(&w[i])) return false;
    return true;
}

/* Time for a thread that could wrongly get through to do so. */
static void settle(void) { usleep(30000); }

/* ---- the structures, read as guest code reads them ----------------------------------------------- */

/* Every read of a lock's memory goes through an atomic load (R23): other threads change these
 * words through the atomics of core/lock.c while a test looks at them, and a plain load
 * beside an atomic store is a data race, which ThreadSanitizer reports. */
#define ALD32(a)         __atomic_load_n((const uint32_t *)(uintptr_t)(a), __ATOMIC_SEQ_CST)
#define ALD64(a)         __atomic_load_n((const uint64_t *)(uintptr_t)(a), __ATOMIC_SEQ_CST)
#define CS_LOCK(cs)      ALD32(addr(cs) + 0x08)
#define CS_RECUR(cs)     ALD32(addr(cs) + 0x0C)
#define CS_OWNER(cs)     ALD64(addr(cs) + 0x10)
#define CS_WORD(cs)      ALD32(addr(cs) + 0x18)      /* 0 free, 1 held, 2 held and contended */
#define SRW_WORD(l)      ALD32(addr(l))              /* bit 0 exclusive, bit 1 waiters, shared count above */
#define CV_SEQ(cv)       ALD32(addr(cv))
#define CV_SLEEPERS(cv)  ALD32(addr(cv) + 4)

#define T_CS_FREE(cs) do { T_EQ(CS_LOCK(cs), 0xFFFFFFFFu); T_EQ(CS_RECUR(cs), 0); T_EQ(CS_OWNER(cs), 0); \
                           T_EQ(CS_WORD(cs), 0); } while (0)

static Worker workers[16];

/* ==== CRITICAL_SECTION ============================================================================ */

typedef struct {
    CRITICAL_SECTION cs;
    uint64_t counter;               /* protected by cs: plain memory */
    int      inside;                /* threads between their first Enter and last Leave */
    int      acquired, arrived;
    unsigned iterations;
} CsShared;

/* The caller has just entered for the first time. */
static void cs_inside(Worker *w, CsShared *s) {
    CRITICAL_SECTION *cs = &s->cs;
    W_CHECK(w, __atomic_exchange_n(&s->inside, 1, __ATOMIC_SEQ_CST) == 0);
    W_CHECK(w, CS_OWNER(cs) == w->id && CS_RECUR(cs) == 1 && CS_LOCK(cs) == 0);
    uint32_t word = CS_WORD(cs);
    W_CHECK(w, word == 1 || word == 2);
    if (word == 2) w->n[1]++;

    /* A read-modify-write of plain memory that another thread inside would tear, now and then
     * held across a reschedule so that the others have to sleep. */
    uint64_t c = s->counter;
    if ((w->n[0] & 63) == 0) sched_yield();
    if ((w->n[0] & 7) == 3) {
        rt_cs_enter(W_CTX(w), cs);
        W_CHECK(w, CS_RECUR(cs) == 2 && CS_OWNER(cs) == w->id);
        W_CHECK(w, rt_cs_try_enter(W_CTX(w), cs) == TRUE);
        W_CHECK(w, CS_RECUR(cs) == 3 && CS_OWNER(cs) == w->id);
        rt_cs_leave(W_CTX(w), cs);
        rt_cs_leave(W_CTX(w), cs);
        W_CHECK(w, CS_RECUR(cs) == 1 && CS_OWNER(cs) == w->id);
    }
    s->counter = c + 1;
    w->n[0]++;
    W_CHECK(w, __atomic_exchange_n(&s->inside, 0, __ATOMIC_SEQ_CST) == 1);
}

/* Time spent outside a lock, so that it changes hands: a thread that takes a lock straight
 * back after releasing it would keep it for its whole run while the others only wake and go
 * back to sleep. */
static void outside(Worker *w) {
    uint64_t r = rnd(w);
    if ((r & 7) == 0) sched_yield();
    else for (volatile unsigned k = (unsigned)(r >> 8) & 63; k; k--) { }
}

static void cs_stress_worker(Worker *w) {
    CsShared *s = w->arg;
    for (unsigned i = 0; i < s->iterations; i++) {
        if (i % 5 == 4) {
            if (!rt_cs_try_enter(W_CTX(w), &s->cs)) { w->n[2]++; outside(w); continue; }
        } else {
            rt_cs_enter(W_CTX(w), &s->cs);
        }
        cs_inside(w, s);
        rt_cs_leave(W_CTX(w), &s->cs);
        outside(w);
    }
}

static void cs_stress(DWORD spin, unsigned nthreads, unsigned iterations) {
    static CsShared s;
    memset(&s, 0, sizeof s);
    rt_cs_init(&s.cs, spin);
    s.iterations = iterations;
    astore(&gate, 0);
    workers_start(workers, nthreads, cs_stress_worker, &s);
    astore(&gate, 1);
    if (!workers_finish(workers, nthreads, "critical section stress")) return;

    /* No increment was lost, so no two threads were inside together. */
    T_EQ(s.counter, tally(workers, nthreads, 0));
    T_EQ(tally(workers, nthreads, 0) + tally(workers, nthreads, 2), (uint64_t)nthreads * iterations);
    T_TRUE(s.counter > 0);
    T_EQ(aload(&s.inside), 0);
    T_CS_FREE(&s.cs);
    T_EQ(ALD64(addr(&s.cs) + 0x20), spin);
    /* The test is only worth something if threads really got in each other's way: some found
     * the section held (TryEnter said no), and without spinning some took it in the contended
     * state, that is after sleeping or with a sleeper behind them. */
    T_TRUE(tally(workers, nthreads, 2) > 0);
    if (spin == 0) T_TRUE(tally(workers, nthreads, 1) > 0);
}

TEST(core, lock_cs_stress)
{
    /* No spinning: every thread that finds the section held goes to sleep. */
    cs_stress(0, 8, 8000);
    cs_stress(0, 2, 30000);
    cs_stress(0, 11, 3000);         /* more threads than performance cores */
}

/* R22: a thread that finds the section held tries SpinCount times before it marks the
 * section contended and sleeps.  What shows it is the thread's own processor time inside
 * EnterCriticalSection, which does not depend on what else the machine is doing: 16.7
 * million tries (the largest count) cost many milliseconds, and a thread that does not spin
 * is asleep after microseconds.  The holder leaves when the word reads 2, that is when the
 * spinner has given up, however long that takes: no deadline, nothing to be late for. */
typedef struct { CRITICAL_SECTION cs; uint64_t cpu_ns; } SpinShared;

static void spin_worker(Worker *w) {
    SpinShared *s = w->arg;
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
    rt_cs_enter(W_CTX(w), &s->cs);
    s->cpu_ns = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID) - t0;
    rt_cs_leave(W_CTX(w), &s->cs);
}

TEST(core, lock_cs_spins_before_it_sleeps)
{
    static SpinShared s;
    memset(&s, 0, sizeof s);
    rt_cs_init(&s.cs, 0x00FFFFFF);
    rt_cs_enter(T_CTX(), &s.cs);
    astore(&gate, 1);
    workers_start(workers, 1, spin_worker, &s);
    while (CS_WORD(&s.cs) != 2) usleep(200);
    rt_cs_leave(T_CTX(), &s.cs);
    if (!workers_finish(workers, 1, "a spinning thread")) return;
    T_TRUE(s.cpu_ns >= 1000000);        /* 1 ms: far more than a thread that sleeps at once, far less than the tries cost */
    T_CS_FREE(&s.cs);
    rt_cs_delete(&s.cs);
}

TEST(core, lock_cs_stress_spinning)
{
    cs_stress(4000, 8, 8000);       /* the spin count the engine's hot sections ask for */
    cs_stress(0x00FFFFFF, 3, 8000); /* the largest: in effect a spin lock */
    cs_stress(1, 8, 4000);
}

static void cs_block_worker(Worker *w) {
    CsShared *s = w->arg;
    W_CHECK(w, rt_cs_try_enter(W_CTX(w), &s->cs) == FALSE);
    __atomic_fetch_add(&s->arrived, 1, __ATOMIC_SEQ_CST);
    rt_cs_enter(W_CTX(w), &s->cs);
    cs_inside(w, s);
    __atomic_fetch_add(&s->acquired, 1, __ATOMIC_SEQ_CST);
    sched_yield();
    rt_cs_leave(W_CTX(w), &s->cs);
}

TEST(core, lock_cs_blocking)
{
    enum { N = 5 };
    Ctx *ctx = T_CTX();
    static CsShared s;
    for (unsigned spin = 0; spin <= 4000; spin += 4000) {
        memset(&s, 0, sizeof s);
        rt_cs_init(&s.cs, spin);
        rt_cs_enter(ctx, &s.cs);
        astore(&gate, 1);
        workers_start(workers, N, cs_block_worker, &s);

        /* All five are asleep behind us: the wait word says "contended", and nobody is inside. */
        T_TRUE(EVENTUALLY(aload(&s.arrived) == N && CS_WORD(&s.cs) == 2 && all_blocked(workers, N)));
        settle();
        T_EQ(aload(&s.acquired), 0);
        T_EQ(CS_OWNER(&s.cs), RT_FIRST_THREAD_ID);
        T_EQ(CS_RECUR(&s.cs), 1);
        T_EQ(CS_LOCK(&s.cs), 0);

        /* A recursive Enter and its Leave by the owner release nothing. */
        rt_cs_enter(ctx, &s.cs);
        T_EQ(rt_cs_try_enter(ctx, &s.cs), TRUE);
        rt_cs_leave(ctx, &s.cs);
        rt_cs_leave(ctx, &s.cs);
        settle();
        T_EQ(aload(&s.acquired), 0);
        T_EQ(CS_RECUR(&s.cs), 1);
        T_EQ(CS_WORD(&s.cs), 2);

        /* The last Leave lets them through, one after another, every one of them. */
        rt_cs_leave(ctx, &s.cs);
        if (!workers_finish(workers, N, "critical section blocking")) return;
        T_EQ(aload(&s.acquired), N);
        T_EQ(s.counter, N);
        T_CS_FREE(&s.cs);
    }
}

/* ==== SRWLOCK ====================================================================================== */

typedef struct {
    SRWLOCK  l;
    uint64_t a, b;                  /* protected: equal whenever no writer is inside.  Plain memory */
    int      readers, writers;      /* threads inside in each mode */
    int      acquired, arrived, got, release;
    unsigned iterations, nwriters;
} SrwShared;

static void srw_write(Worker *w, SrwShared *s, bool yield) {
    W_CHECK(w, __atomic_exchange_n(&s->writers, 1, __ATOMIC_SEQ_CST) == 0);
    W_CHECK(w, aload(&s->readers) == 0);
    W_CHECK(w, (SRW_WORD(&s->l) & ~2u) == 1);
    uint64_t a = s->a;
    s->a = a + 1;
    if (yield) sched_yield();
    W_CHECK(w, s->b == a);
    s->b = a + 1;
    W_CHECK(w, aload(&s->readers) == 0);
    W_CHECK(w, __atomic_exchange_n(&s->writers, 0, __ATOMIC_SEQ_CST) == 1);
    w->n[0]++;
}

static void srw_read(Worker *w, SrwShared *s, bool yield) {
    uint64_t now = (uint64_t)__atomic_fetch_add(&s->readers, 1, __ATOMIC_SEQ_CST) + 1;
    if (now > w->n[3]) w->n[3] = now;
    W_CHECK(w, aload(&s->writers) == 0);
    uint32_t word = SRW_WORD(&s->l);
    W_CHECK(w, !(word & 1) && (word >> 2) >= 1);
    uint64_t a = s->a;
    if (yield) sched_yield();
    W_CHECK(w, s->b == a);          /* a writer inside would have moved one of them */
    W_CHECK(w, aload(&s->writers) == 0);
    __atomic_fetch_sub(&s->readers, 1, __ATOMIC_SEQ_CST);
    w->n[1]++;
}

static void srw_stress_worker(Worker *w) {
    SrwShared *s = w->arg;
    bool writer = w->index < s->nwriters;
    for (unsigned i = 0; i < s->iterations; i++) {
        bool try = i % 4 == 3;
        if (writer) {
            if (try) { if (!rt_srw_try_acquire_exclusive(&s->l)) { w->n[2]++; continue; } }
            else rt_srw_acquire_exclusive(&s->l);
            srw_write(w, s, (i & 31) == 0);
            rt_srw_release_exclusive(&s->l);
        } else {
            if (try) { if (!rt_srw_try_acquire_shared(&s->l)) { w->n[2]++; continue; } }
            else rt_srw_acquire_shared(&s->l);
            srw_read(w, s, (i & 63) == 0);
            rt_srw_release_shared(&s->l);
        }
        outside(w);
    }
}

static void srw_stress(unsigned nthreads, unsigned nwriters, unsigned iterations) {
    static SrwShared s;
    memset(&s, 0, sizeof s);        /* an all-zero SRWLOCK: never initialised by a call */
    s.iterations = iterations;
    s.nwriters = nwriters;
    astore(&gate, 0);
    workers_start(workers, nthreads, srw_stress_worker, &s);
    astore(&gate, 1);
    if (!workers_finish(workers, nthreads, "SRW lock stress")) return;
    T_EQ(s.a, tally(workers, nthreads, 0));
    T_EQ(s.b, s.a);
    T_EQ(tally(workers, nthreads, 0) + tally(workers, nthreads, 1) + tally(workers, nthreads, 2),
         (uint64_t)nthreads * iterations);
    T_TRUE(nwriters == 0 || s.a > 0);
    T_TRUE(nwriters == nthreads || tally(workers, nthreads, 1) > 0);
    /* The threads really got in each other's way: with a writer about, some Try calls said no;
     * and readers really were inside together. */
    if (nwriters) T_TRUE(tally(workers, nthreads, 2) > 0);
    uint64_t together = 0;
    for (unsigned i = 0; i < nthreads; i++) if (workers[i].n[3] > together) together = workers[i].n[3];
    if (nthreads - nwriters > 1) T_TRUE(together > 1);
    T_EQ(aload(&s.readers), 0);
    T_EQ(aload(&s.writers), 0);
    T_EQ(ALD64(addr(&s.l)), 0);
}

TEST(core, lock_srw_stress_exclusive)
{
    srw_stress(8, 8, 4000);         /* writers only: the lock as a mutex */
    srw_stress(2, 2, 20000);
}

TEST(core, lock_srw_stress_shared)
{
    srw_stress(8, 1, 8000);         /* many readers and one writer */
    srw_stress(8, 3, 5000);
    srw_stress(11, 2, 3000);        /* more threads than performance cores */
    srw_stress(6, 0, 15000);        /* readers only: the count */
}

static void srw_hold_shared_worker(Worker *w) {
    SrwShared *s = w->arg;
    rt_srw_acquire_shared(&s->l);
    astore(&s->got, 1);
    while (!aload(&s->release)) usleep(100);
    rt_srw_release_shared(&s->l);
}

static void srw_block_worker(Worker *w) {
    SrwShared *s = w->arg;
    bool writer = w->index < s->nwriters;
    __atomic_fetch_add(&s->arrived, 1, __ATOMIC_SEQ_CST);
    if (writer) {
        rt_srw_acquire_exclusive(&s->l);
        srw_write(w, s, true);
        __atomic_fetch_add(&s->acquired, 1, __ATOMIC_SEQ_CST);
        rt_srw_release_exclusive(&s->l);
    } else {
        rt_srw_acquire_shared(&s->l);
        srw_read(w, s, true);
        __atomic_fetch_add(&s->acquired, 1, __ATOMIC_SEQ_CST);
        rt_srw_release_shared(&s->l);
    }
}

TEST(core, lock_srw_blocking)
{
    static SrwShared s;
    static Worker late[4];
    memset(&s, 0, sizeof s);
    astore(&gate, 1);

    /* Readers share: a second thread gets in while the first still holds the lock. */
    rt_srw_acquire_shared(&s.l);
    workers_start(workers, 1, srw_hold_shared_worker, &s);
    T_TRUE(EVENTUALLY(aload(&s.got)));
    T_EQ(SRW_WORD(&s.l), 8);
    astore(&s.release, 1);
    if (!workers_finish(workers, 1, "SRW shared with shared")) return;
    T_EQ(SRW_WORD(&s.l), 4);

    /* A writer waits for the reader, and sets the waiters bit. */
    s.nwriters = 1;
    workers_start(workers, 1, srw_block_worker, &s);
    T_TRUE(EVENTUALLY(SRW_WORD(&s.l) == (4 | 2) && all_blocked(workers, 1)));
    settle();
    T_EQ(aload(&s.acquired), 0);
    T_EQ(SRW_WORD(&s.l), 4 | 2);
    /* A new reader does not overtake the waiting writer (as on Windows): it queues too. */
    T_EQ(rt_srw_try_acquire_shared(&s.l), FALSE);
    T_EQ(rt_srw_try_acquire_exclusive(&s.l), FALSE);
    s.nwriters = 0;
    workers_start(late, 2, srw_block_worker, &s);
    T_TRUE(EVENTUALLY(aload(&s.arrived) == 3 && all_blocked(late, 2)));
    settle();
    T_EQ(aload(&s.acquired), 0);
    T_EQ(SRW_WORD(&s.l), 4 | 2);
    /* The last shared release frees the lock and wakes all three. */
    rt_srw_release_shared(&s.l);
    if (!workers_finish(workers, 1, "SRW writer behind a reader")) return;
    if (!workers_finish(late, 2, "SRW readers behind a waiting writer")) return;
    T_EQ(aload(&s.acquired), 3);
    T_EQ(s.a, 1);
    T_EQ(ALD64(addr(&s.l)), 0);

    /* Everybody waits for a writer: three readers and two writers behind an exclusive holder. */
    astore(&s.acquired, 0);
    astore(&s.arrived, 0);
    rt_srw_acquire_exclusive(&s.l);
    s.nwriters = 2;
    workers_start(workers, 5, srw_block_worker, &s);
    T_TRUE(EVENTUALLY(aload(&s.arrived) == 5 && SRW_WORD(&s.l) == (1 | 2) && all_blocked(workers, 5)));
    settle();
    T_EQ(aload(&s.acquired), 0);
    T_EQ(SRW_WORD(&s.l), 1 | 2);
    T_EQ(rt_srw_try_acquire_shared(&s.l), FALSE);
    T_EQ(rt_srw_try_acquire_exclusive(&s.l), FALSE);
    rt_srw_release_exclusive(&s.l);
    if (!workers_finish(workers, 5, "SRW behind a writer")) return;
    T_EQ(aload(&s.acquired), 5);
    T_EQ(s.a, 3);
    T_EQ(s.b, 3);
    T_EQ(ALD64(addr(&s.l)), 0);
}

/* ==== CONDITION_VARIABLE =========================================================================== */

/* The lock a condition variable is used with: a critical section, or an SRW lock held
 * exclusively. */
typedef struct {
    bool             use_cs;
    CRITICAL_SECTION cs;
    SRWLOCK          srw;
} Lk;

static void lk_init(Lk *l, bool use_cs) {
    memset(l, 0, sizeof *l);
    l->use_cs = use_cs;
    if (use_cs) rt_cs_init(&l->cs, 0);
}
static void lk_lock(Lk *l, Ctx *ctx)   { if (l->use_cs) rt_cs_enter(ctx, &l->cs); else rt_srw_acquire_exclusive(&l->srw); }
static void lk_unlock(Lk *l, Ctx *ctx) { if (l->use_cs) rt_cs_leave(ctx, &l->cs); else rt_srw_release_exclusive(&l->srw); }
static BOOL lk_sleep(Lk *l, Ctx *ctx, CONDITION_VARIABLE *cv, DWORD ms) {
    return l->use_cs ? rt_cv_sleep_cs(ctx, cv, &l->cs, ms) : rt_cv_sleep_srw(ctx, cv, &l->srw, ms, 0);
}
/* Does the thread with this id hold the lock (as far as the structure can say)? */
static bool lk_held(Lk *l, uint32_t id) {
    return l->use_cs ? CS_OWNER(&l->cs) == id && CS_RECUR(&l->cs) == 1 : (SRW_WORD(&l->srw) & 1) != 0;
}
static bool lk_free(Lk *l) {
    return l->use_cs ? CS_OWNER(&l->cs) == 0 && CS_WORD(&l->cs) == 0 && CS_RECUR(&l->cs) == 0 : ALD64(addr(&l->srw)) == 0;
}

#define SENTINEL 0x0BAD0BADu

/* One Sleep, with everything checked that must hold whenever it returns: the lock is held
 * again; TRUE leaves last error alone; FALSE happens only with a timeout and sets
 * ERROR_TIMEOUT.  n[4] counts wake-ups, n[5] timeouts. */
static BOOL checked_sleep(Worker *w, Lk *l, CONDITION_VARIABLE *cv, DWORD ms) {
    rt_set_last_error(W_CTX(w), SENTINEL);
    BOOL ok = lk_sleep(l, W_CTX(w), cv, ms);
    W_CHECK(w, ok == TRUE || ok == FALSE);
    W_CHECK(w, lk_held(l, w->id));
    if (ok) {
        W_CHECK(w, rt_get_last_error(W_CTX(w)) == SENTINEL);
        w->n[4]++;
    } else {
        W_CHECK(w, ms != INFINITE);
        W_CHECK(w, rt_get_last_error(W_CTX(w)) == ERROR_TIMEOUT);
        w->n[5]++;
    }
    return ok;
}

/* ---- ping-pong: two threads take turns, each move needs the other's wake ---------------------- */

typedef struct {
    Lk                 lock;
    CONDITION_VARIABLE cv;
    int                turn;        /* protected: whose move it is */
    uint64_t           moves;       /* protected */
    unsigned           rounds;
    bool               wake_inside; /* wake before releasing the lock, or after */
    bool               wake_all;
    DWORD              timeout;
    unsigned           stall_every; /* every nth move is late, so that the other thread times out */
} PingPong;

static void ping_pong_worker(Worker *w) {
    PingPong *s = w->arg;
    int me = (int)w->index;
    for (unsigned r = 0; r < s->rounds; r++) {
        /* Late on purpose now and then, the two threads at different rounds: the other one is
         * then waiting for this move and its sleep runs out. */
        if (s->stall_every && (r + (unsigned)me * (s->stall_every / 2)) % s->stall_every == s->stall_every - 1)
            usleep(3000);
        lk_lock(&s->lock, W_CTX(w));
        while (s->turn != me) checked_sleep(w, &s->lock, &s->cv, s->timeout);
        W_CHECK(w, s->moves % 2 == (uint64_t)me);
        s->moves++;
        s->turn = 1 - me;
        w->n[0]++;
        if (s->wake_inside) { if (s->wake_all) rt_cv_wake_all(&s->cv); else rt_cv_wake(&s->cv); }
        lk_unlock(&s->lock, W_CTX(w));
        if (!s->wake_inside) { if (s->wake_all) rt_cv_wake_all(&s->cv); else rt_cv_wake(&s->cv); }
    }
}

/* Returns the number of timeouts, or -1 if the threads did not finish. */
static int64_t ping_pong(bool use_cs, bool wake_inside, bool wake_all, DWORD timeout, unsigned stall_every,
                         unsigned rounds) {
    static PingPong s;
    memset(&s, 0, sizeof s);        /* an all-zero CONDITION_VARIABLE: never initialised by a call */
    lk_init(&s.lock, use_cs);
    s.rounds = rounds;
    s.wake_inside = wake_inside;
    s.wake_all = wake_all;
    s.timeout = timeout;
    s.stall_every = stall_every;
    astore(&gate, 0);
    workers_start(workers, 2, ping_pong_worker, &s);
    astore(&gate, 1);
    if (!workers_finish(workers, 2, "condition variable ping-pong")) return -1;
    T_EQ(s.moves, 2 * (uint64_t)rounds);
    T_EQ(workers[0].n[0], rounds);
    T_EQ(workers[1].n[0], rounds);
    T_TRUE(lk_free(&s.lock));
    T_EQ(CV_SLEEPERS(&s.cv), 0);
    if (timeout == INFINITE) {
        T_EQ(tally(workers, 2, 5), 0);
        /* Nearly every move found the other thread asleep and had to wake it. */
        T_TRUE(tally(workers, 2, 4) > rounds);
    }
    return (int64_t)tally(workers, 2, 5);
}

TEST(core, lock_cv_ping_pong)
{
    /* Without timeouts: a single lost wake-up stops the game for good. */
    for (int use_cs = 0; use_cs < 2; use_cs++)
        for (int inside = 0; inside < 2; inside++)
            for (int all = 0; all < 2; all++)
                if (ping_pong(use_cs, inside, all, INFINITE, 0, 8000) < 0) return;
}

TEST(core, lock_cv_ping_pong_timeouts)
{
    for (int use_cs = 0; use_cs < 2; use_cs++) {
        /* 1 ms timeouts, with a partner that is 3 ms late every 50th move: both ways out of
         * Sleep are taken, and each must be told apart correctly. */
        int64_t timeouts = ping_pong(use_cs, false, false, 1, 50, 1500);
        if (timeouts < 0) return;
        T_TRUE(timeouts > 0);
        T_TRUE(tally(workers, 2, 4) > 0);
        /* Timeout 0 never waits: the loop polls, and every FALSE is a timeout. */
        if (ping_pong(use_cs, true, true, 0, 0, 300) < 0) return;
        T_TRUE(tally(workers, 2, 5) > 0);
        /* The longest finite timeout behaves like no timeout. */
        timeouts = ping_pong(use_cs, false, false, 0xFFFFFFFE, 0, 1000);
        if (timeouts < 0) return;
        T_EQ(timeouts, 0);
    }
}

/* ---- producers and consumers: a bounded queue with two condition variables --------------------- */

enum { QCAP = 4 };

typedef struct {
    Lk                 lock;
    CONDITION_VARIABLE not_empty, not_full;
    uint32_t           queue[QCAP]; /* protected */
    unsigned           head, count; /* protected */
    bool               closed;      /* protected: no more items will come */
    unsigned           nproducers, per_producer;
    DWORD              timeout;
    unsigned           producer_stall, consumer_stall;  /* every nth item, 3 ms late: the other side times out */
} Pc;

static void pc_worker(Worker *w) {
    Pc *s = w->arg;
    Ctx *ctx = W_CTX(w);
    if (w->index < s->nproducers) {
        for (unsigned k = 1; k <= s->per_producer; k++) {
            uint32_t item = (w->index + 1) * 1000000u + k;
            if (s->producer_stall && k % s->producer_stall == 0) usleep(3000);
            lk_lock(&s->lock, ctx);
            while (s->count == QCAP) checked_sleep(w, &s->lock, &s->not_full, s->timeout);
            s->queue[(s->head + s->count++) % QCAP] = item;
            lk_unlock(&s->lock, ctx);
            rt_cv_wake(&s->not_empty);
            w->n[0]++;
            w->n[1] += item;
        }
        return;
    }
    for (;;) {
        lk_lock(&s->lock, ctx);
        while (s->count == 0 && !s->closed) checked_sleep(w, &s->lock, &s->not_empty, s->timeout);
        if (s->count == 0) { lk_unlock(&s->lock, ctx); return; }
        uint32_t item = s->queue[s->head];
        s->head = (s->head + 1) % QCAP;
        s->count--;
        lk_unlock(&s->lock, ctx);
        rt_cv_wake(&s->not_full);
        w->n[2]++;
        w->n[3] += item;
        if (s->consumer_stall && w->n[2] % s->consumer_stall == 0) usleep(3000);
    }
}

static void producer_consumer(bool use_cs, DWORD timeout, unsigned nproducers, unsigned nconsumers, unsigned per,
                              unsigned producer_stall, unsigned consumer_stall) {
    static Pc s;
    memset(&s, 0, sizeof s);
    s.producer_stall = producer_stall;
    s.consumer_stall = consumer_stall;
    lk_init(&s.lock, use_cs);
    rt_cv_init(&s.not_empty);
    rt_cv_init(&s.not_full);
    s.nproducers = nproducers;
    s.per_producer = per;
    s.timeout = timeout;
    unsigned n = nproducers + nconsumers;
    astore(&gate, 0);
    workers_start(workers, n, pc_worker, &s);
    astore(&gate, 1);

    /* Every producer gets all its items in: no wake of not_full was lost. */
    if (!workers_finish(workers, nproducers, "producers")) return;
    lk_lock(&s.lock, T_CTX());
    s.closed = true;
    lk_unlock(&s.lock, T_CTX());
    rt_cv_wake_all(&s.not_empty);
    /* Every consumer sees the end: no wake of not_empty was lost. */
    if (!workers_finish(workers + nproducers, nconsumers, "consumers")) return;

    /* Each item was taken exactly once. */
    T_EQ(tally(workers, n, 0), (uint64_t)nproducers * per);
    T_EQ(tally(workers, n, 2), tally(workers, n, 0));
    T_EQ(tally(workers, n, 3), tally(workers, n, 1));
    T_EQ(s.count, 0);
    T_TRUE(lk_free(&s.lock));
    T_EQ(CV_SLEEPERS(&s.not_empty), 0);
    T_EQ(CV_SLEEPERS(&s.not_full), 0);
    T_TRUE(tally(workers, n, 4) > 0);                   /* threads did sleep and were woken */
    if (timeout == INFINITE) T_EQ(tally(workers, n, 5), 0);
    if (producer_stall || consumer_stall) T_TRUE(tally(workers, n, 5) > 0);     /* and sleeps did time out */
}

TEST(core, lock_cv_producer_consumer)
{
    producer_consumer(true, INFINITE, 3, 3, 20000, 0, 0);
    producer_consumer(false, INFINITE, 3, 3, 20000, 0, 0);
    producer_consumer(true, INFINITE, 1, 5, 20000, 0, 0);   /* many sleepers on one variable, woken one at a time */
    producer_consumer(false, INFINITE, 5, 1, 20000, 0, 0);
    /* The same with 1 ms sleeps and one side that is late now and then, so that sleeps time
     * out while wakes are flying: consumers starve, then producers find the queue full. */
    producer_consumer(true, 1, 1, 3, 3000, 150, 0);
    producer_consumer(false, 1, 1, 3, 3000, 150, 0);
    producer_consumer(true, 1, 3, 1, 1000, 0, 150);
    producer_consumer(false, 1, 3, 1, 1000, 0, 150);
}

/* ---- wake one, wake all, shared mode ------------------------------------------------------------ */

typedef struct {
    SRWLOCK            l;
    CONDITION_VARIABLE cv;
    int                flag;        /* protected by l */
    int                wakeups;     /* returns from Sleep, counted by the sleepers */
    ULONG              flags;       /* 0, or LOCKMODE_SHARED */
    DWORD              timeouts[2]; /* by index parity */
} Wake;

static void wake_sleeper(Worker *w) {
    Wake *s = w->arg;
    bool shared = (s->flags & CONDITION_VARIABLE_LOCKMODE_SHARED) != 0;
    if (shared) rt_srw_acquire_shared(&s->l); else rt_srw_acquire_exclusive(&s->l);
    while (!s->flag) {
        rt_set_last_error(W_CTX(w), SENTINEL);
        BOOL ok = rt_cv_sleep_srw(W_CTX(w), &s->cv, &s->l, s->timeouts[w->index & 1], s->flags);
        W_CHECK(w, ok == TRUE);
        W_CHECK(w, rt_get_last_error(W_CTX(w)) == SENTINEL);
        uint32_t word = SRW_WORD(&s->l);
        if (shared) W_CHECK(w, !(word & 1) && (word >> 2) >= 1);
        else W_CHECK(w, (word & 1) && (word >> 2) == 0);
        __atomic_fetch_add(&s->wakeups, 1, __ATOMIC_SEQ_CST);
    }
    if (shared) rt_srw_release_shared(&s->l); else rt_srw_release_exclusive(&s->l);
}

/* `seq` is the wake sequence the variable starts with, as if that many wakes had gone before. */
static void wake_one_and_all(ULONG flags, uint32_t seq) {
    enum { N = 6 };
    static Wake s;
    memset(&s, 0, sizeof s);
    ST32(addr(&s.cv), seq);
    s.flags = flags;
    s.timeouts[0] = INFINITE;
    s.timeouts[1] = 0xFFFFFFFE;     /* the longest finite timeout: 49.7 days */
    astore(&gate, 1);
    workers_start(workers, N, wake_sleeper, &s);

    /* All six are asleep inside Sleep, and each released the lock to get there. */
    T_TRUE(EVENTUALLY(CV_SLEEPERS(&s.cv) == N && SRW_WORD(&s.l) == 0 && all_blocked(workers, N)));
    settle();
    T_EQ(aload(&s.wakeups), 0);
    T_EQ(CV_SEQ(&s.cv), seq);
    T_TRUE(all_blocked(workers, N));

    /* Wake releases one sleeper: at least one always, and exactly one when all of them are
     * asleep in the kernel, as they are here.  (A sleeper that has not got that far yet comes
     * back too, which Windows allows; that is why "exactly one" is asked of one round in
     * three and not of each.)  The condition is still false, so whoever woke sleeps again. */
    bool exactly_one = false;
    uint32_t wakes = 0;
    for (int round = 0; round < 3; round++) {
        int before = aload(&s.wakeups);
        rt_cv_wake(&s.cv);
        wakes++;
        T_TRUE(EVENTUALLY(aload(&s.wakeups) > before));
        T_TRUE(EVENTUALLY(CV_SLEEPERS(&s.cv) == N && SRW_WORD(&s.l) == 0 && all_blocked(workers, N)));
        settle();
        int woke = aload(&s.wakeups) - before;
        T_TRUE(woke >= 1 && woke <= N);
        exactly_one = exactly_one || woke == 1;
    }
    T_TRUE(exactly_one);
    /* The sequence moved once per wake that found sleepers; when it wraps, the count of
     * sleepers in the upper half is not disturbed. */
    T_EQ(CV_SEQ(&s.cv), (uint32_t)(seq + wakes));
    T_EQ(CV_SLEEPERS(&s.cv), N);

    /* WakeAll releases every one of them. */
    rt_srw_acquire_exclusive(&s.l);
    s.flag = 1;
    rt_srw_release_exclusive(&s.l);
    rt_cv_wake_all(&s.cv);
    if (!workers_finish(workers, N, "WakeAllConditionVariable")) return;
    T_TRUE(aload(&s.wakeups) >= N + (int)wakes);
    T_EQ(CV_SLEEPERS(&s.cv), 0);
    T_EQ(CV_SEQ(&s.cv), (uint32_t)(seq + wakes + 1));
    T_EQ(ALD64(addr(&s.l)), 0);

    /* With nobody asleep any more, wakes change nothing. */
    rt_cv_wake(&s.cv);
    rt_cv_wake_all(&s.cv);
    T_EQ(CV_SEQ(&s.cv), (uint32_t)(seq + wakes + 1));
    T_EQ(CV_SLEEPERS(&s.cv), 0);
}

TEST(core, lock_cv_wake_one_and_all)
{
    wake_one_and_all(0, 0);
    wake_one_and_all(CONDITION_VARIABLE_LOCKMODE_SHARED, 0);
    /* The 32-bit sequence wraps during the test: 0xFFFFFFFE, 0xFFFFFFFF, 0, 1, 2. */
    wake_one_and_all(0, 0xFFFFFFFE);
}

/* A wake issued without the lock, after a flag was set without the lock, still reaches a
 * thread that is already inside Sleep: the sleeper registers and reads the sequence in one
 * atomic step. */
typedef struct {
    CRITICAL_SECTION   cs;
    CONDITION_VARIABLE cv;
    int                flag;        /* atomic, NOT protected by cs */
    unsigned           rounds;
} Naked;

static void naked_sleeper(Worker *w) {
    Naked *s = w->arg;
    for (unsigned r = 0; r < s->rounds; r++) {
        rt_cs_enter(W_CTX(w), &s->cs);
        while (aload(&s->flag) != (int)(r + 1)) {
            /* Bounded, so that a missed wake is a failed check and not a hang: the waker
             * issues exactly one wake per round, after we are inside Sleep. */
            BOOL ok = rt_cv_sleep_cs(W_CTX(w), &s->cv, &s->cs, 60000);
            W_CHECK(w, ok == TRUE);
            if (!ok) break;
        }
        rt_cs_leave(W_CTX(w), &s->cs);
        __atomic_fetch_add(&w->n[0], 1, __ATOMIC_SEQ_CST);     /* the test reads it while this thread runs */
    }
}

TEST(core, lock_cv_wake_without_lock)
{
    static Naked s;
    memset(&s, 0, sizeof s);
    rt_cs_init(&s.cs, 0);
    s.rounds = 300;
    astore(&gate, 1);
    workers_start(workers, 1, naked_sleeper, &s);
    bool ok = true;
    for (unsigned r = 0; r < s.rounds && ok; r++) {
        /* The sleeper is registered (it may or may not have reached the kernel): one wake. */
        ok = EVENTUALLY(CV_SLEEPERS(&s.cv) == 1);
        astore(&s.flag, (int)(r + 1));
        rt_cv_wake(&s.cv);
        ok = ok && EVENTUALLY(__atomic_load_n(&workers[0].n[0], __ATOMIC_SEQ_CST) == r + 1 || aload(&workers[0].done));
    }
    T_TRUE(ok);
    if (!workers_finish(workers, 1, "wake without the lock")) return;
    T_EQ(workers[0].n[0], s.rounds);
    T_EQ(CV_SEQ(&s.cv), s.rounds);
    T_EQ(CV_SLEEPERS(&s.cv), 0);
}

/* ---- signals ----------------------------------------------------------------------------------- */
/* A signal delivered to a thread that is asleep on a lock or a condition variable interrupts
 * the kernel wait.  That is not a wake-up: the thread must neither get the lock nor return
 * from Sleep, and must still be woken properly afterwards. */

static int signals_seen;

static void on_signal(int sig) {
    (void)sig;
    __atomic_fetch_add(&signals_seen, 1, __ATOMIC_SEQ_CST);
}

/* Signal every worker a number of times, and wait until handlers have run. */
static bool pester(Worker *w, unsigned n) {
    int before = aload(&signals_seen);
    for (int round = 0; round < 25; round++) {
        for (unsigned i = 0; i < n; i++) pthread_kill(w[i].host, SIGUSR1);
        usleep(1000);
    }
    return EVENTUALLY(aload(&signals_seen) >= before + (int)n);
}

TEST(core, lock_signals_while_asleep)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;      /* no SA_RESTART: the interrupted call is to return */
    sigemptyset(&sa.sa_mask);
    T_REQUIRE(sigaction(SIGUSR1, &sa, NULL) == 0);
    astore(&gate, 1);

    /* Asleep in EnterCriticalSection. */
    static CsShared c;
    memset(&c, 0, sizeof c);
    rt_cs_init(&c.cs, 0);
    rt_cs_enter(T_CTX(), &c.cs);
    workers_start(workers, 3, cs_block_worker, &c);
    T_TRUE(EVENTUALLY(aload(&c.arrived) == 3 && CS_WORD(&c.cs) == 2 && all_blocked(workers, 3)));
    T_TRUE(pester(workers, 3));
    T_EQ(aload(&c.acquired), 0);
    T_EQ(CS_OWNER(&c.cs), RT_FIRST_THREAD_ID);
    T_TRUE(EVENTUALLY(all_blocked(workers, 3)));
    rt_cs_leave(T_CTX(), &c.cs);
    if (!workers_finish(workers, 3, "critical section after signals")) return;
    T_EQ(aload(&c.acquired), 3);
    T_CS_FREE(&c.cs);

    /* Asleep in AcquireSRWLockExclusive and AcquireSRWLockShared. */
    static SrwShared r;
    memset(&r, 0, sizeof r);
    r.nwriters = 2;
    rt_srw_acquire_exclusive(&r.l);
    workers_start(workers, 4, srw_block_worker, &r);
    T_TRUE(EVENTUALLY(aload(&r.arrived) == 4 && SRW_WORD(&r.l) == (1 | 2) && all_blocked(workers, 4)));
    T_TRUE(pester(workers, 4));
    T_EQ(aload(&r.acquired), 0);
    T_EQ(SRW_WORD(&r.l), 1 | 2);
    T_TRUE(EVENTUALLY(all_blocked(workers, 4)));
    rt_srw_release_exclusive(&r.l);
    if (!workers_finish(workers, 4, "SRW lock after signals")) return;
    T_EQ(aload(&r.acquired), 4);
    T_EQ(ALD64(addr(&r.l)), 0);

    /* Asleep in SleepConditionVariableSRW: the interrupted wait is not passed on as a
     * spurious wake-up, with or without a timeout. */
    static Wake v;
    memset(&v, 0, sizeof v);
    v.timeouts[0] = INFINITE;
    v.timeouts[1] = 0xFFFFFFFE;
    workers_start(workers, 4, wake_sleeper, &v);
    T_TRUE(EVENTUALLY(CV_SLEEPERS(&v.cv) == 4 && SRW_WORD(&v.l) == 0 && all_blocked(workers, 4)));
    T_TRUE(pester(workers, 4));
    T_EQ(aload(&v.wakeups), 0);
    T_EQ(CV_SLEEPERS(&v.cv), 4);
    T_EQ(CV_SEQ(&v.cv), 0);
    T_TRUE(EVENTUALLY(all_blocked(workers, 4)));
    rt_srw_acquire_exclusive(&v.l);
    v.flag = 1;
    rt_srw_release_exclusive(&v.l);
    rt_cv_wake_all(&v.cv);
    if (!workers_finish(workers, 4, "condition variable after signals")) return;
    T_EQ(aload(&v.wakeups), 4);
    T_EQ(CV_SLEEPERS(&v.cv), 0);
}

/* ==== SLIST_HEADER ================================================================================= */

typedef struct {
    SLIST_ENTRY entry;              /* first, as in the game's own nodes */
    int         owned;              /* 1 while some thread holds the node off the list */
    uint32_t    seen;
} __attribute__((aligned(16))) Node;

typedef struct {
    SLIST_HEADER head;
    Node        *nodes;
    unsigned     nnodes, iterations;
    int          consumed;          /* transfer test: nodes taken for good */
    unsigned     nproducers, per_producer;
} SlShared;

/* Take a node that has just come off the list: nobody else may hold it. */
static Node *node_claim(Worker *w, PSLIST_ENTRY e) {
    Node *n = (Node *)e;
    W_CHECK(w, __atomic_exchange_n(&n->owned, 1, __ATOMIC_SEQ_CST) == 0);
    return n;
}

static void node_push(Worker *w, SlShared *s, Node *n) {
    W_CHECK(w, __atomic_exchange_n(&n->owned, 0, __ATOMIC_SEQ_CST) == 1);
    rt_slist_push(&s->head, &n->entry);
}

/* Pop, hold a few nodes, push them back in another order, flush now and then: the pattern
 * that produces ABA (a node leaves and returns with a different successor while another
 * thread is between reading the header and swapping it). */
static void slist_stress_worker(Worker *w) {
    SlShared *s = w->arg;
    enum { STASH = 8 };
    Node *stash[STASH];
    unsigned held = 0;
    Node **chain = malloc(s->nnodes * sizeof *chain);
    for (unsigned i = 0; i < s->iterations; i++) {
        uint64_t r = rnd(w);
        unsigned op = (unsigned)(r % 64);
        if (op == 0) {
            /* The flushed chain is ours alone: it must be whole, and free of nodes held elsewhere. */
            unsigned len = 0;
            for (PSLIST_ENTRY e = rt_slist_flush(&s->head), next; e; e = next) {
                next = e->Next;
                if (len == s->nnodes) { W_CHECK(w, !"a flushed chain longer than every node there is"); break; }
                chain[len++] = node_claim(w, e);
            }
            while (len) node_push(w, s, chain[--len]);
            w->n[2]++;
        } else if (op < 32) {
            PSLIST_ENTRY e = rt_slist_pop(&s->head);
            if (!e) { w->n[3]++; continue; }
            Node *n = node_claim(w, e);
            w->n[0]++;
            if (held < STASH) stash[held++] = n;
            else node_push(w, s, n);
        } else if (held) {
            unsigned k = (unsigned)(r >> 8) % held;
            Node *n = stash[k];
            stash[k] = stash[--held];
            node_push(w, s, n);
            w->n[1]++;
        }
        W_CHECK(w, rt_slist_depth(&s->head) <= s->nnodes);
    }
    while (held) node_push(w, s, stash[--held]);
    free(chain);
}

/* ABA made on purpose, at full rate.  A "churner" keeps one spare node and repeats: pop A,
 * pop B, push the spare, push A; B is the next spare.  After each round the list has the same
 * first entry A and the same depth as before, but A's successor is a different node and the
 * old successor is in the churner's hands.  A thread that read "first is A, next is B" before
 * the round and swaps the header after it would put a node on the list that somebody holds,
 * and lose one: only the sequence in the header tells the two states apart.  The other
 * threads ("victims") just pop and push back, as often as they can. */
static void slist_aba_worker(Worker *w) {
    SlShared *s = w->arg;
    if (w->index < s->nproducers) {
        /* `iterations` rounds that really happened: an empty list (the others hold every node)
         * is waited out, not counted. */
        Node *spare = NULL;
        while (w->n[0] < s->iterations) {
            PSLIST_ENTRY ea = rt_slist_pop(&s->head);
            if (!ea) { w->n[3]++; sched_yield(); continue; }
            Node *a = node_claim(w, ea);
            PSLIST_ENTRY eb = rt_slist_pop(&s->head);
            Node *b = eb ? node_claim(w, eb) : NULL;
            if (spare) node_push(w, s, spare);
            node_push(w, s, a);
            spare = b;
            w->n[0]++;
        }
        if (spare) node_push(w, s, spare);
        __atomic_fetch_add(&s->consumed, 1, __ATOMIC_SEQ_CST);     /* one churner fewer */
        return;
    }
    /* A victim keeps going for as long as any churner does (and long enough to have done
     * something even if it was only scheduled late). */
    while (aload(&s->consumed) < (int)s->nproducers || w->n[1] < 1000) {
        PSLIST_ENTRY e = rt_slist_pop(&s->head);
        if (!e) { w->n[3]++; continue; }
        node_push(w, s, node_claim(w, e));
        w->n[1]++;
    }
}

/* The list must hold every node exactly once. */
static void slist_expect_all(SlShared *s) {
    for (unsigned i = 0; i < s->nnodes; i++) s->nodes[i].seen = 0;
    unsigned len = 0;
    bool in_range = true;
    for (PSLIST_ENTRY e = (PSLIST_ENTRY)(uintptr_t)ALD64(addr(&s->head) + 8); e && len <= s->nnodes; e = e->Next) {
        Node *n = (Node *)e;
        if (n < s->nodes || n >= s->nodes + s->nnodes) { in_range = false; break; }
        n->seen++;
        len++;
    }
    T_TRUE(in_range);
    T_EQ(len, s->nnodes);                       /* none lost */
    unsigned once = 0, owned = 0;
    for (unsigned i = 0; i < s->nnodes; i++) { once += s->nodes[i].seen == 1; owned += s->nodes[i].owned != 0; }
    T_EQ(once, s->nnodes);                      /* none twice */
    T_EQ(owned, 0);
    T_EQ(rt_slist_depth(&s->head), (USHORT)s->nnodes);
}

/* `churners` = 0 runs the mixed workload; otherwise that many churners and the rest victims. */
static void slist_stress(unsigned nthreads, unsigned nnodes, unsigned iterations, unsigned churners) {
    static SlShared s;
    memset(&s, 0, sizeof s);        /* an all-zero SLIST_HEADER: never initialised by a call */
    s.nproducers = churners;
    T_REQUIRE(posix_memalign((void **)&s.nodes, 16, nnodes * sizeof *s.nodes) == 0);
    memset(s.nodes, 0, nnodes * sizeof *s.nodes);
    s.nnodes = nnodes;
    s.iterations = iterations;
    for (unsigned i = 0; i < nnodes; i++) rt_slist_push(&s.head, &s.nodes[i].entry);
    slist_expect_all(&s);

    astore(&gate, 0);
    workers_start(workers, nthreads, churners ? slist_aba_worker : slist_stress_worker, &s);
    astore(&gate, 1);
    if (!workers_finish(workers, nthreads, churners ? "SList ABA" : "SList stress")) return;
    T_TRUE(tally(workers, nthreads, 0) > 0 && tally(workers, nthreads, 1) > 0);
    T_TRUE(churners || tally(workers, nthreads, 2) > 0);
    slist_expect_all(&s);

    /* And they all come off again. */
    unsigned popped = 0;
    while (popped <= nnodes && rt_slist_pop(&s.head)) popped++;     /* bounded: a corrupted list may be a cycle */
    T_EQ(popped, nnodes);
    T_EQ(rt_slist_depth(&s.head), 0);
    T_EQ(ALD64(addr(&s.head) + 8), 0);
    free(s.nodes);
}

TEST(core, lock_slist_stress)
{
    slist_stress(8, 256, 400000, 0);
    slist_stress(8, 4, 400000, 0);  /* a short list: the same few nodes come and go all the time */
    slist_stress(3, 1, 300000, 0);  /* one node: the list is empty half the time */
    slist_stress(11, 64, 150000, 0); /* more threads than performance cores */
}

TEST(core, lock_slist_aba)
{
    /* Three nodes and one churner: the first entry is the same node after every round, so a
     * thread that was held up in mid-pop finds "its" first entry there again half the time. */
    slist_stress(4, 3, 400000, 1);
    slist_stress(16, 3, 200000, 1); /* more threads than cores: threads are descheduled in mid-operation */
    slist_stress(8, 8, 300000, 2);
    slist_stress(16, 6, 150000, 3);
}

/* One way: producers push fresh nodes, consumers pop them (or flush them) for good. */
static void slist_transfer_worker(Worker *w) {
    SlShared *s = w->arg;
    unsigned total = s->nproducers * s->per_producer;
    if (w->index < s->nproducers) {
        Node *mine = s->nodes + w->index * s->per_producer;
        for (unsigned k = 0; k < s->per_producer; k++) {
            mine[k].owned = 0;
            rt_slist_push(&s->head, &mine[k].entry);
            if ((k & 1023) == 0) sched_yield();
        }
        return;
    }
    bool flusher = w->index == s->nproducers;       /* the first consumer flushes, the others pop */
    while (aload(&s->consumed) < (int)total) {
        if (flusher) {
            unsigned len = 0;
            for (PSLIST_ENTRY e = rt_slist_flush(&s->head), next; e && len <= total; e = next) {
                next = e->Next;
                node_claim(w, e);
                len++;
            }
            W_CHECK(w, len <= total);
            if (len) __atomic_fetch_add(&s->consumed, (int)len, __ATOMIC_SEQ_CST);
            w->n[0] += len;
        } else {
            PSLIST_ENTRY e = rt_slist_pop(&s->head);
            if (!e) continue;
            node_claim(w, e);
            __atomic_fetch_add(&s->consumed, 1, __ATOMIC_SEQ_CST);
            w->n[0]++;
        }
    }
}

TEST(core, lock_slist_transfer)
{
    enum { PRODUCERS = 4, CONSUMERS = 4, PER = 50000 };
    static SlShared s;
    memset(&s, 0, sizeof s);
    rt_slist_init(&s.head);
    s.nproducers = PRODUCERS;
    s.per_producer = PER;
    s.nnodes = PRODUCERS * PER;
    T_REQUIRE(posix_memalign((void **)&s.nodes, 16, s.nnodes * sizeof *s.nodes) == 0);
    memset(s.nodes, 0, s.nnodes * sizeof *s.nodes);

    astore(&gate, 0);
    workers_start(workers, PRODUCERS + CONSUMERS, slist_transfer_worker, &s);
    astore(&gate, 1);
    if (!workers_finish(workers, PRODUCERS + CONSUMERS, "SList transfer")) return;

    /* Every node pushed was taken exactly once, and nothing is left. */
    T_EQ(aload(&s.consumed), PRODUCERS * PER);
    T_EQ(tally(workers + PRODUCERS, CONSUMERS, 0), PRODUCERS * PER);
    unsigned owned = 0;
    for (unsigned i = 0; i < s.nnodes; i++) owned += s.nodes[i].owned == 1;
    T_EQ(owned, s.nnodes);
    T_TRUE(rt_slist_pop(&s.head) == NULL);
    T_EQ(rt_slist_depth(&s.head), 0);
    T_EQ(ALD64(addr(&s.head) + 8), 0);
    free(s.nodes);
}
