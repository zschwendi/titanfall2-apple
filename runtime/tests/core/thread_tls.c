/* TLS slots (runtime/core/thread_tls.c; the specification is rt/thread.h, "TLS slots").
 *
 * Typed from the numbered test plan of docs/handoff/rt-contract-thread.md, case for case: case N
 * of the plan is the one block T_PLAN(N, ...).  Each TEST runs in its own process.  The guest
 * threads record what they see in statics; only the main thread uses the T_ macros.
 */
#include "rt/test.h"
#include "wait_util.c"
#include "../../core/thread_priv.h"
#include <pthread.h>
#include <sched.h>

#define SENTINEL 0x5EA1u

static RtThreadObject *object_of(HANDLE h) { return (RtThreadObject *)wu_obj(h); }

/* ---- what the guest threads of the cases record ------------------------------------------ */

static HANDLE   blk_go;                     /* manual-reset event the blocked thread waits for */
static int      blk_started;                /* set by the blocked thread after it has recorded */
static uint64_t blk_get3, blk_teb, blk_exp; /* its rt_tls_get(3); its teb (W); its LD64(teb + 0x1780) (WE) */

static DWORD    plain_index;                /* what rt_tls_alloc returned on a plain host thread */

#define CHURN_THREADS 8
#define CHURN_ROUNDS  2000
static uint32_t churn_fails[CHURN_THREADS]; /* failed checks, one count per guest thread */

/* ---- guest functions and host functions run on other threads ----------------------------- */

GUEST_FN(tls_blocked)
{
    uint64_t teb = ctx->gs_base;
    blk_get3 = rt_tls_get(ctx, 3);
    rt_tls_set(ctx, 3, 0xB3);
    rt_tls_set(ctx, 100, 0xB100);
    blk_teb = teb;
    blk_exp = LD64(teb + 0x1780);
    wu_store(&blk_started, 1);
    (void)rt_wait_handle(ctx, blk_go, INFINITE, FALSE);
    ctx->r[0] = 0x55;                       /* the exit code the main thread expects */
    GUEST_RET(ctx);
}

static void *plain_alloc(void *arg)
{
    (void)arg;
    plain_index = rt_tls_alloc();
    return NULL;
}

GUEST_FN(tls_churn)
{
    uint32_t n = (uint32_t)ctx->r[1];
    uint64_t tag = 0x7A60u + n;
    for (unsigned rnd = 1; rnd <= CHURN_ROUNDS; rnd++) {
        DWORD i = rt_tls_alloc();
        rt_tls_set(ctx, i, tag);
        if (rt_tls_get(ctx, i) != tag) churn_fails[n]++;
        if (rt_tls_free(i) != TRUE) churn_fails[n]++;
        if (rnd % 16 == 0) sched_yield();
    }
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* ---- the cases ---------------------------------------------------------------------------- */

TEST(core, thread_tls)
{
    T_PLAN(1, "first three allocations are 0, 1, 2") {
        DWORD a = rt_tls_alloc();
        DWORD b = rt_tls_alloc();
        DWORD c = rt_tls_alloc();
        T_EQ(a, 0);
        T_EQ(b, 1);
        T_EQ(c, 2);
    }

    T_PLAN(2, "a set stores into the thread block") {
        Ctx *ctx = T_CTX();
        uint64_t teb = ctx->gs_base;
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_tls_set(ctx, 0, 0x1111), TRUE);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(LD64(teb + 0x1480), 0x1111);
        T_EQ(rt_tls_get(ctx, 0), 0x1111);
    }

    T_PLAN(3, "an unset slot reads 0 with success") {
        Ctx *ctx = T_CTX();
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_tls_get(ctx, 1), 0);
        T_EQ(T_LAST_ERROR(), 0);
    }

    T_PLAN(4, "slot addresses below 64 and the create flag") {
        Ctx *ctx = T_CTX();
        uint64_t teb = ctx->gs_base;
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_tls_slot(ctx, 5, false), teb + 0x1480 + 40);
        T_EQ(rt_tls_slot(ctx, 63, true), teb + 0x1480 + 504);
        T_EQ(T_LAST_ERROR(), SENTINEL);
    }

    T_PLAN(5, "a read of index 64 makes no expansion array") {
        Ctx *ctx = T_CTX();
        uint64_t teb = ctx->gs_base;
        T_EQ(LD64(teb + 0x1780), 0);
        T_EQ(rt_tls_slot(ctx, 64, false), 0);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_tls_get(ctx, 64), 0);
        T_EQ(T_LAST_ERROR(), 0);
        T_EQ(LD64(teb + 0x1780), 0);
    }

    T_PLAN(6, "a set of index 64 makes the expansion array") {
        Ctx *ctx = T_CTX();
        uint64_t teb = ctx->gs_base;
        T_EQ(rt_tls_set(ctx, 64, 7), TRUE);
        uint64_t E = LD64(teb + 0x1780);
        T_REQUIRE(E != 0);
        T_EQ(rt_tls_slot(ctx, 64, false), E);
        T_EQ(rt_tls_slot(ctx, 1087, false), E + 8184);
        T_EQ(LD64(E), 7);
        T_EQ(rt_tls_get(ctx, 64), 7);
        uint32_t nonzero = 0;
        for (uint64_t k = 1; k <= 1023; k++) {
            if (LD64(E + 8 * k) != 0) nonzero++;
        }
        T_EQ(nonzero, 0);
    }

    T_PLAN(7, "indices 1088 and 0xFFFFFFFF are refused") {
        Ctx *ctx = T_CTX();
        DWORD bad[2] = { 1088, 0xFFFFFFFFu };
        for (unsigned k = 0; k < 2; k++) {
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_tls_get(ctx, bad[k]), 0);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_tls_set(ctx, bad[k], 1), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_tls_slot(ctx, bad[k], true), 0);
            T_EQ(T_LAST_ERROR(), SENTINEL);
        }
    }

    T_PLAN(8, "an unallocated index stores and reads") {
        Ctx *ctx = T_CTX();
        T_EQ(rt_tls_set(ctx, 500, 9), TRUE);
        T_EQ(rt_tls_get(ctx, 500), 9);
    }

    T_PLAN(9, "free of index 1088, 0xFFFFFFFF or a never allocated index fails") {
        Ctx *ctx = T_CTX();
        DWORD bad[3] = { 1088, 0xFFFFFFFFu, 700 };
        for (unsigned k = 0; k < 3; k++) {
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_tls_free(bad[k]), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        }
    }
}

TEST(core, thread_tls_reuse)
{
    T_PLAN(10, "free zeroes the slot, a second free fails, the lowest free index comes back") {
        Ctx *ctx = T_CTX();
        uint64_t teb = ctx->gs_base;
        DWORD a = rt_tls_alloc();
        DWORD b = rt_tls_alloc();
        DWORD c = rt_tls_alloc();
        T_EQ(a, 0);
        T_EQ(b, 1);
        T_EQ(c, 2);
        T_EQ(rt_tls_set(ctx, b, 5), TRUE);
        T_EQ(LD64(teb + 0x1480 + 8 * (uint64_t)b), 5);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_tls_free(b), TRUE);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(LD64(teb + 0x1480 + 8 * (uint64_t)b), 0);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_tls_free(b), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        DWORD again = rt_tls_alloc();
        T_EQ(again, b);
        T_EQ(rt_tls_get(ctx, b), 0);
    }
}

TEST(core, thread_tls_exhaust)
{
    T_PLAN(11, "1088 allocations in order, then the fatal") {
        for (DWORD i = 0; i < 1088; i++) {
            DWORD got = rt_tls_alloc();
            T_REQUIRE(got == i);
        }
        T_TRAPS(rt_tls_alloc(), "all 1088 TLS indices are allocated");
    }
}

TEST(core, thread_tls_blocked)
{
    T_PLAN(12, "a new thread's slots are its own; a free reaches every running thread") {
        Ctx *ctx = T_CTX();
        uint64_t teb = ctx->gs_base;
        for (DWORD i = 0; i <= 100; i++) {
            DWORD got = rt_tls_alloc();
            T_REQUIRE(got == i);
        }
        T_EQ(rt_tls_set(ctx, 3, 0xAAA), TRUE);
        blk_go = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(blk_go != NULL);
        wu_store(&blk_started, 0);
        HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(tls_blocked), 0, 0, NULL);
        T_REQUIRE(h != NULL);
        T_TRUE(EVENTUALLY(wu_load(&blk_started)));
        T_EQ(blk_get3, 0);                              /* a new thread's slots are zero */
        uint64_t W = blk_teb, WE = blk_exp;
        T_TRUE(WE != 0);
        T_EQ(LD64(W + 0x1480 + 24), 0xB3);
        T_EQ(LD64(teb + 0x1480 + 24), 0xAAA);
        if (WE != 0) {
            T_EQ(LD64(WE + 8 * 36), 0xB100);
        }
        T_EQ(rt_tls_free(3), TRUE);
        T_EQ(rt_tls_free(100), TRUE);
        T_EQ(LD64(W + 0x1480 + 24), 0);
        if (WE != 0) {
            T_EQ(LD64(WE + 8 * 36), 0);
        }
        T_EQ(LD64(teb + 0x1480 + 24), 0);
        T_EQ(LD64(teb + 0x1780), 0);                    /* freeing made no array for a thread that had none */
        T_EQ(rt_event_set(ctx, blk_go), TRUE);
        T_EQ(rt_wait_handle(ctx, h, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(object_of(h)->exit_code, 0x55);
    }
}

TEST(core, thread_tls_host)
{
    T_PLAN(13, "a plain host thread gets an index main does not hold") {
        DWORD held[2];
        held[0] = rt_tls_alloc();
        held[1] = rt_tls_alloc();
        pthread_t th;
        T_REQUIRE(pthread_create(&th, NULL, plain_alloc, NULL) == 0);
        T_EQ(pthread_join(th, NULL), 0);
        T_TRUE(plain_index < 1088);
        T_TRUE(plain_index != held[0] && plain_index != held[1]);
    }
}

TEST(core, thread_tls_concurrent)
{
    T_PLAN(14, "eight guest threads allocate, set, get and free at once") {
        Ctx *ctx = T_CTX();
        HANDLE h[CHURN_THREADS];
        for (uint32_t n = 0; n < CHURN_THREADS; n++) {
            h[n] = rt_thread_create(ctx, 0, T_GUEST_ADDR(tls_churn), n, 0, NULL);
            T_REQUIRE(h[n] != NULL);
        }
        T_EQ(rt_wait_handles(ctx, CHURN_THREADS, h, TRUE, INFINITE, FALSE), WAIT_OBJECT_0);
        for (uint32_t n = 0; n < CHURN_THREADS; n++) T_EQ(churn_fails[n], 0);
        T_EQ(rt_thread_count(), 1);
    }
}
