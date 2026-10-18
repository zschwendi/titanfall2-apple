/* FLS slots (runtime/core/thread_fls.c; the specification is rt/thread.h, "FLS slots", and
 * rt_fls_thread_exit in core/thread_priv.h).
 *
 * Typed from the numbered test plan of docs/handoff/rt-contract-thread.md (row F), case for case:
 * case N of the plan is the one block T_PLAN(N, ...).  The corrections the header made after the
 * plan are in the cases themselves: rt_fls_get sets ERROR_SUCCESS when it succeeds (cases 2, 3,
 * 4, 5), case 2 has the set-over-a-value check, and case 4 stores where the plan trapped.  Each
 * TEST runs in its own process; the cases that need a fresh process (1 and 11) come first in
 * their TEST.  The callbacks and guest threads record what they see in statics; only the main
 * thread uses the T_ macros, and it reads those statics only after the thread that wrote them
 * has been joined, or has been waited for with EVENTUALLY.
 */
#include "rt/test.h"
#include "wait_util.c"
#include "../../core/thread_priv.h"
#include <pthread.h>
#include <sched.h>

#define SENTINEL 0x5EA1u

static RtThreadObject *object_of(HANDLE h) { return (RtThreadObject *)wu_obj(h); }

/* ---- what the callbacks and the guest threads record ------------------------------------- */

#define FLS_CALLS_MAX 64
static uint64_t  fls_val[FLS_CALLS_MAX];    /* the value of each callback call, in the order the calls ran */
static RtThread *fls_on[FLS_CALLS_MAX];     /* rt_thread_self() of the thread each call ran on */
static uint32_t  fls_ncalls;                /* calls so far; claimed with an atomic add: calls may run on any thread */
static uint32_t  fls_bad;                   /* failed FLS calls made by callbacks and threads where none may fail */

static void fls_record(uint64_t v)
{
    uint32_t k = __atomic_fetch_add(&fls_ncalls, 1, __ATOMIC_SEQ_CST);
    if (k < FLS_CALLS_MAX) {
        fls_val[k] = v;
        fls_on[k]  = rt_thread_self();
    }
}

static void fls_failed(void) { __atomic_fetch_add(&fls_bad, 1, __ATOMIC_SEQ_CST); }

static DWORD    thr_idx[3];                 /* cases 7 and 9: the indices a thread sets; main writes them before it starts the thread */
static DWORD    fls_store_to;               /* case 9: the lower index that fls_cb_store sets to 0x99 */
static HANDLE   blk_go;                     /* manual-reset event the blocked thread waits for */
static int      blk_started;                /* set by the blocked thread after it has set its value */
static uint64_t blk_after;                  /* the blocked thread's rt_fls_get of its index, after it was released */

#define CHURN_THREADS 8
#define CHURN_ROUNDS  2000
static uint32_t churn_fails[CHURN_THREADS]; /* failed checks, one count per guest thread */
static DWORD    race_index;                 /* case 13, second part: the index set by eight threads and freed by main */
static uint32_t race_set_count;             /* how many of the eight have set race_index */
static uint32_t race_fails[CHURN_THREADS];  /* failed sets, one count per guest thread */

static DWORD plain_idx;                     /* case 12: what rt_fls_alloc returned on a plain host thread */
static DWORD plain_free_idx;                /* case 12: the index a plain host thread tries to free */

/* ---- callbacks and guest threads ---------------------------------------------------------- */

/* The callback `cb` of the plan: records its value and its thread, then calls rt_fls_alloc(0)
 * and rt_fls_free on the result from inside itself, which proves that no lock is held across a
 * callback. */
GUEST_FN(fls_cb)
{
    fls_record(ctx->r[1]);
    DWORD j = rt_fls_alloc(0);
    if (rt_fls_free(ctx, j) != TRUE) fls_failed();
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 9: a callback that stores 0x99 at the lower index fls_store_to during the exit pass. */
GUEST_FN(fls_cb_store)
{
    fls_record(ctx->r[1]);
    if (rt_fls_set(ctx, fls_store_to, 0x99) != TRUE) fls_failed();
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 7: sets 0x10, 0x20, 0x30 at thr_idx[0..2], then returns. */
GUEST_FN(fls_set3)
{
    for (unsigned k = 0; k < 3; k++)
        if (rt_fls_set(ctx, thr_idx[k], 0x10 * (k + 1)) != TRUE) fls_failed();
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 9: sets 0x40 and 0x41 at thr_idx[0] and thr_idx[1], then returns. */
GUEST_FN(fls_set2)
{
    for (unsigned k = 0; k < 2; k++)
        if (rt_fls_set(ctx, thr_idx[k], 0x40 + k) != TRUE) fls_failed();
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 8: sets nothing. */
GUEST_FN(fls_nothing)
{
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 10: sets its index (the parameter) to 0x51, records that it has, waits for blk_go, then
 * reads the index back. */
GUEST_FN(fls_blocked)
{
    DWORD i = (DWORD)ctx->r[1];
    if (rt_fls_set(ctx, i, 0x51) != TRUE) fls_failed();
    wu_store(&blk_started, 1);
    (void)rt_wait_handle(ctx, blk_go, INFINITE, FALSE);
    blk_after = rt_fls_get(ctx, i);
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 13, first part: 2,000 rounds of alloc, set, get, free on one index each. */
GUEST_FN(fls_churn)
{
    uint32_t n = (uint32_t)ctx->r[1];
    uint64_t tag = 0x7A60u + n;
    for (unsigned rnd = 1; rnd <= CHURN_ROUNDS; rnd++) {
        DWORD i = rt_fls_alloc(0);
        if (rt_fls_set(ctx, i, tag) != TRUE) churn_fails[n]++;
        if (rt_fls_get(ctx, i) != tag) churn_fails[n]++;
        if (rt_fls_free(ctx, i) != TRUE) churn_fails[n]++;
        if (rnd % 16 == 0) sched_yield();
    }
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 13, second part: sets 0x100 + n at race_index, announces it, then yields a varying
 * number of times so that its exit lands at different points of the main thread's free. */
GUEST_FN(fls_race)
{
    uint32_t n = (uint32_t)ctx->r[1];
    if (rt_fls_set(ctx, race_index, 0x100 + n) != TRUE) race_fails[n]++;
    __atomic_fetch_add(&race_set_count, 1, __ATOMIC_SEQ_CST);
    for (uint32_t k = 0; k < 3 * n; k++) sched_yield();
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Case 12: a plain host thread (not a guest thread) allocates, and then frees with no ctx. */
static void *plain_alloc(void *arg)
{
    (void)arg;
    plain_idx = rt_fls_alloc(0);
    return NULL;
}

static void *plain_free(void *arg)
{
    (void)arg;
    (void)rt_fls_free(NULL, plain_free_idx);
    return NULL;
}

static void plain_free_on_host(void)
{
    pthread_t th;
    if (pthread_create(&th, NULL, plain_free, NULL) == 0) (void)pthread_join(th, NULL);
}

/* ---- the cases ---------------------------------------------------------------------------- */

TEST(core, thread_fls)
{
    T_PLAN(1, "first two allocations are 1, then 2") {
        T_EQ(rt_fls_alloc(0), 1);
        T_EQ(rt_fls_alloc(0), 2);
    }

    T_PLAN(2, "a set stores a value; a get reads it and sets ERROR_SUCCESS") {
        Ctx *ctx = T_CTX();
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_fls_set(ctx, 1, 0xABC), TRUE);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(rt_fls_get(ctx, 1), 0xABC);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_fls_get(ctx, 2), 0);                    /* index 2 was never set */
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
        rt_set_last_error(ctx, 0xdeadbeef);
        T_EQ(rt_fls_set(ctx, 1, 0x217), TRUE);
        T_EQ(rt_fls_get(ctx, 1), 0x217);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    }

    T_PLAN(3, "indices 0, 128 and 0xFFFFFFFF are refused; a never-allocated index reads 0") {
        Ctx *ctx = T_CTX();
        DWORD bad[3] = { 0, RT_FLS_SLOTS, 0xFFFFFFFFu };
        for (unsigned k = 0; k < 3; k++) {
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_fls_get(ctx, bad[k]), 0);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_fls_set(ctx, bad[k], 1), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_fls_free(ctx, bad[k]), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        }
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_fls_free(ctx, 100), FALSE);             /* never allocated */
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_fls_get(ctx, 100), 0);                  /* checked before case 4 stores there */
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    }

    T_PLAN(4, "a set of an index never allocated stores the value and returns TRUE") {
        Ctx *ctx = T_CTX();
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_fls_set(ctx, 100, 1), TRUE);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(rt_fls_get(ctx, 100), 1);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    }

    T_PLAN(5, "free without a callback zeroes the value; the lowest free index comes back") {
        Ctx *ctx = T_CTX();
        DWORD i = rt_fls_alloc(0);
        T_EQ(rt_fls_set(ctx, i, 5), TRUE);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_fls_free(ctx, i), TRUE);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(rt_fls_alloc(0), i);
        T_EQ(rt_fls_get(ctx, i), 0);
    }
}

TEST(core, thread_fls_callbacks)
{
    T_PLAN(6, "free calls the callback once, with the value, on the freeing thread; a zero value is not called") {
        Ctx *ctx = T_CTX();
        fls_ncalls = 0;
        DWORD i = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        T_EQ(rt_fls_set(ctx, i, 0x77), TRUE);
        T_EQ(rt_fls_free(ctx, i), TRUE);
        T_EQ(fls_ncalls, 1);
        T_EQ(fls_val[0], 0x77);
        T_TRUE(fls_on[0] == RT_THREAD(ctx));
        T_EQ(rt_fls_get(ctx, i), 0);
        DWORD j = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        T_EQ(rt_fls_free(ctx, j), TRUE);                /* value 0: no call */
        T_EQ(fls_ncalls, 1);
        T_EQ(__atomic_load_n(&fls_bad, __ATOMIC_SEQ_CST), 0);
    }

    T_PLAN(7, "a thread's values are called at its exit in index order, on that thread; no callback, no call") {
        Ctx *ctx = T_CTX();
        DWORD i = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        DWORD j = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        DWORD k = rt_fls_alloc(0);
        thr_idx[0] = i;
        thr_idx[1] = j;
        thr_idx[2] = k;
        fls_ncalls = 0;
        HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(fls_set3), 0, 0, NULL);
        T_REQUIRE(h != NULL);
        T_EQ(rt_wait_handle(ctx, h, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(fls_ncalls, 2);
        T_EQ(fls_val[0], 0x10);
        T_EQ(fls_val[1], 0x20);
        T_TRUE(fls_on[0] == object_of(h)->thread);
        T_TRUE(fls_on[1] == object_of(h)->thread);
        T_EQ(__atomic_load_n(&fls_bad, __ATOMIC_SEQ_CST), 0);
    }

    T_PLAN(8, "a thread that sets nothing ends with no call") {
        Ctx *ctx = T_CTX();
        (void)rt_fls_alloc(T_GUEST_ADDR(fls_cb));       /* allocated with a callback, never set by the thread */
        fls_ncalls = 0;
        HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(fls_nothing), 0, 0, NULL);
        T_REQUIRE(h != NULL);
        T_EQ(rt_wait_handle(ctx, h, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(fls_ncalls, 0);
    }

    T_PLAN(9, "a callback that stores a lower index during the exit pass: the value is dropped and logged") {
        Ctx *ctx = T_CTX();
        DWORD i = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        DWORD j = rt_fls_alloc(T_GUEST_ADDR(fls_cb_store));
        T_TRUE(i < j);
        thr_idx[0] = i;
        thr_idx[1] = j;
        fls_store_to = i;
        fls_ncalls = 0;
        HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(fls_set2), 0, 0, NULL);
        T_REQUIRE(h != NULL);
        T_EQ(rt_wait_handle(ctx, h, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(fls_ncalls, 2);
        T_EQ(fls_val[0], 0x40);
        T_EQ(fls_val[1], 0x41);
        T_TRUE(fls_on[0] == object_of(h)->thread);
        T_TRUE(fls_on[1] == object_of(h)->thread);
        T_TRUE(T_LOGGED(RT_CH_THREAD, "FLS value set during thread exit is dropped"));
        T_EQ(__atomic_load_n(&fls_bad, __ATOMIC_SEQ_CST), 0);
    }

    T_PLAN(10, "free of a value a blocked thread set: both are called on the freeing thread; the thread then sees 0") {
        Ctx *ctx = T_CTX();
        DWORD i = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        blk_go = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(blk_go != NULL);
        wu_store(&blk_started, 0);
        HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(fls_blocked), i, 0, NULL);
        T_REQUIRE(h != NULL);
        T_TRUE(EVENTUALLY(wu_load(&blk_started)));
        fls_ncalls = 0;
        T_EQ(rt_fls_set(ctx, i, 0x52), TRUE);
        T_EQ(rt_fls_free(ctx, i), TRUE);
        T_EQ(fls_ncalls, 2);
        T_EQ(fls_val[0], 0x52);                         /* the freeing thread's own value first */
        T_EQ(fls_val[1], 0x51);
        T_TRUE(fls_on[0] == RT_THREAD(ctx));
        T_TRUE(fls_on[1] == RT_THREAD(ctx));
        T_EQ(rt_event_set(ctx, blk_go), TRUE);
        T_EQ(rt_wait_handle(ctx, h, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(blk_after, 0);
        T_EQ(fls_ncalls, 2);                            /* the thread's exit calls nothing more */
        T_EQ(__atomic_load_n(&fls_bad, __ATOMIC_SEQ_CST), 0);
    }
}

TEST(core, thread_fls_exhaust)
{
    T_PLAN(11, "127 allocations return 1 .. 127 in order, then the fatal") {
        for (DWORD i = 1; i <= 127; i++) {
            DWORD got = rt_fls_alloc(0);
            T_REQUIRE(got == i);
        }
        T_TRAPS(rt_fls_alloc(0), "FlsAlloc: all 127 FLS indices are allocated");
    }
}

TEST(core, thread_fls_host_thread)
{
    T_PLAN(12, "a plain host thread gets a valid index; freeing a due callback from it is fatal") {
        pthread_t th;
        T_REQUIRE(pthread_create(&th, NULL, plain_alloc, NULL) == 0);
        T_EQ(pthread_join(th, NULL), 0);
        T_TRUE(plain_idx >= 1 && plain_idx < RT_FLS_SLOTS);

        Ctx *ctx = T_CTX();
        DWORD i = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        T_EQ(rt_fls_set(ctx, i, 5), TRUE);
        plain_free_idx = i;
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS(plain_free_on_host(), "FlsFree: a callback is due and the calling host thread is not a guest thread");
    }
}

TEST(core, thread_fls_churn)
{
    T_PLAN(13, "eight guest threads alloc, set, get and free at once; a shared index calls each value exactly once") {
        Ctx *ctx = T_CTX();
        HANDLE h[CHURN_THREADS];
        for (uint32_t n = 0; n < CHURN_THREADS; n++) {
            h[n] = rt_thread_create(ctx, 0, T_GUEST_ADDR(fls_churn), n, 0, NULL);
            T_REQUIRE(h[n] != NULL);
        }
        T_EQ(rt_wait_handles(ctx, CHURN_THREADS, h, TRUE, INFINITE, FALSE), WAIT_OBJECT_0);
        for (uint32_t n = 0; n < CHURN_THREADS; n++) T_EQ(churn_fails[n], 0);

        race_index = rt_fls_alloc(T_GUEST_ADDR(fls_cb));
        fls_ncalls = 0;
        HANDLE r[CHURN_THREADS];
        for (uint32_t n = 0; n < CHURN_THREADS; n++) {
            r[n] = rt_thread_create(ctx, 0, T_GUEST_ADDR(fls_race), n, 0, NULL);
            T_REQUIRE(r[n] != NULL);
        }
        T_TRUE(EVENTUALLY(__atomic_load_n(&race_set_count, __ATOMIC_SEQ_CST) == CHURN_THREADS));
        T_EQ(rt_fls_free(ctx, race_index), TRUE);       /* every value was set before this: each is called once */
        T_EQ(rt_wait_handles(ctx, CHURN_THREADS, r, TRUE, INFINITE, FALSE), WAIT_OBJECT_0);
        for (uint32_t n = 0; n < CHURN_THREADS; n++) T_EQ(race_fails[n], 0);
        T_EQ(fls_ncalls, CHURN_THREADS);
        for (uint32_t n = 0; n < CHURN_THREADS; n++) {
            unsigned hits = 0;
            for (uint32_t k = 0; k < fls_ncalls && k < FLS_CALLS_MAX; k++)
                if (fls_val[k] == 0x100 + n) hits++;
            T_EQ(hits, 1);
        }
        T_EQ(__atomic_load_n(&fls_bad, __ATOMIC_SEQ_CST), 0);
        T_EQ(rt_thread_count(), 1);
    }
}
