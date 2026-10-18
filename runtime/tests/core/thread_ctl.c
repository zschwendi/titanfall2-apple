/* Looking at and adjusting a thread (rt_thread_get_exit_code, rt_thread_set_priority,
 * rt_thread_get_priority, rt_thread_set_affinity, rt_thread_open, rt_thread_from_handle).
 * The specification is runtime/include/rt/thread.h; the cases are the numbered plan in
 * docs/handoff/rt-contract-thread.md.  Each TEST runs in its own process.
 */
#include "rt/test.h"
#include "wait_util.c"
#include "rt/vm.h"
#include "../../core/thread_priv.h"

#define SENTINEL 0x5EA1u

/* A blocked thread: sets tc_started, waits for the manual-reset event tc_go, returns 77. */
static HANDLE tc_go;
static int    tc_started;

GUEST_FN(tc_wait_for_go)
{
    wu_store(&tc_started, 1);
    (void)rt_wait_handle(ctx, tc_go, INFINITE, FALSE);
    ctx->r[0] = 77;
    GUEST_RET(ctx);
}

TEST(core, thread_ctl)
{
    Ctx *ctx = T_CTX();
    DWORD c = 0;
    HANDLE h = NULL;

    T_PLAN(1, "blocked thread reports STILL_ACTIVE, then 77") {
        tc_go = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(tc_go != NULL);
        wu_store(&tc_started, 0);
        h = rt_thread_create(ctx, 0, T_GUEST_ADDR(tc_wait_for_go), 0, 0, NULL);
        T_REQUIRE(h != NULL);
        T_TRUE(EVENTUALLY(wu_load(&tc_started)));
        rt_set_last_error(ctx, SENTINEL);
        c = 0;
        T_TRUE(rt_thread_get_exit_code(h, &c));
        T_EQ(c, 259);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        c = 0;
        T_TRUE(rt_thread_get_exit_code(RT_PSEUDO_THREAD, &c));
        T_EQ(c, 259);
        T_EQ(rt_event_set(ctx, tc_go), TRUE);
        T_EQ(rt_wait_handle(ctx, h, INFINITE, FALSE), WAIT_OBJECT_0);
        c = 0;
        T_TRUE(rt_thread_get_exit_code(h, &c));
        T_EQ(c, 77);
        T_EQ(rt_handle_close(tc_go), TRUE);
    }

    T_PLAN(2, "bad handles fail and leave the code alone") {
        HANDLE done = NULL, e = NULL;
        c = 0xABCD;
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_get_exit_code(NULL, &c), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(c, 0xABCD);

        tc_go = rt_event_create(ctx, TRUE, TRUE, NULL);
        T_REQUIRE(tc_go != NULL);
        done = rt_thread_create(ctx, 0, T_GUEST_ADDR(tc_wait_for_go), 0, 0, NULL);
        T_REQUIRE(done != NULL);
        T_EQ(rt_wait_handle(ctx, done, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(rt_handle_close(done), TRUE);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_get_exit_code(done, &c), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(c, 0xABCD);

        e = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(e != NULL);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_get_exit_code(e, &c), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(c, 0xABCD);
        T_EQ(rt_handle_close(e), TRUE);
        T_EQ(rt_handle_close(tc_go), TRUE);
    }

    T_PLAN(3, "NULL code pointer is fatal") {
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS(rt_thread_get_exit_code(RT_PSEUDO_THREAD, NULL), "lpExitCode is NULL");
    }

    T_PLAN(4, "priority is recorded per thread") {
        static const int vals[7] = { -15, -2, -1, 0, 1, 2, 15 };
        T_EQ(rt_thread_get_priority(RT_PSEUDO_THREAD), 0);
        for (unsigned k = 0; k < 7; k++) {
            rt_set_last_error(ctx, SENTINEL);
            T_TRUE(rt_thread_set_priority(h, vals[k]));
            T_EQ(rt_thread_get_priority(h), vals[k]);
            T_EQ(T_LAST_ERROR(), SENTINEL);
            T_EQ(rt_thread_get_priority(RT_PSEUDO_THREAD), 0);
        }
    }

    T_PLAN(5, "priority can be set after the thread has ended") {
        T_TRUE(rt_thread_set_priority(h, 1));
        T_EQ(rt_thread_get_priority(h), 1);
    }

    T_PLAN(6, "priority of an event or NULL is an invalid handle") {
        HANDLE e = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(e != NULL);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_set_priority(e, 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_get_priority(e), 0x7FFFFFFF);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_set_priority(NULL, 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_get_priority(NULL), 0x7FFFFFFF);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(7, "a priority that is not one of the seven is fatal") {
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS(rt_thread_set_priority(RT_PSEUDO_THREAD, 3), "thread priority 3 is not one");
        T_TRAPS(rt_thread_set_priority(RT_PSEUDO_THREAD, -7), "thread priority -7 is not one");
    }
}

/* Fresh process: the main thread has never set a mask. */
TEST(core, thread_ctl_affinity)
{
    Ctx *ctx = T_CTX();
    uint64_t sys = 0;

    T_PLAN(8, "a first mask returns the system mask; the next returns the first; all bits set is all processors") {
        sys = rt_system_affinity();
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 1), sys);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, sys), 1);
        /* Windows (Vista and later) reads all bits set, or only the low 32 bits all set, as all
         * processors: the call succeeds, returns the previous mask and stores the process's mask
         * (Wine kernel32/tests/thread.c, test_thread_processor).  The stored mask is sys before this. */
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 1), sys);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, ~(DWORD_PTR)0), 1);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 1), sys);
        if (sizeof(DWORD_PTR) == 8) {
            T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, ~(DWORD_PTR)0 >> 3), 1);
            T_EQ(T_LAST_ERROR(), SENTINEL);
            T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 1), sys);
        }
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, sys), 1);
    }

    T_PLAN(9, "invalid masks and handles change nothing") {
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 0), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        if (sys != ~(uint64_t)0) {
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, sys + 1), 0);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        }
        HANDLE e = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(e != NULL);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_set_affinity(e, 1), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_handle_close(e), TRUE);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 1), sys);
    }
}

/* A thread that ends at once: the suspended thread of plan 11 runs it when resumed. */
GUEST_FN(tc_return_now) { ctx->r[0] = 0; GUEST_RET(ctx); }

/* Fresh process: the thread has never set a mask, and the process mask is changed here. */
TEST(core, thread_ctl_affinity_process)
{
    Ctx *ctx = T_CTX();

    T_PLAN(10, "a thread without its own mask has the process's mask") {
        rt_dispatcher_lock();
        rt_process_object()->affinity = 1;
        rt_process_object()->affinity_changes++;
        rt_dispatcher_unlock();
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 2), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 1), 1);
    }

    /* Windows gives every thread the new process mask, one that set its own too, and one created
     * suspended (Wine kernel32/tests/thread.c, test_thread_processor, where SetThreadGroupAffinity
     * exists, i.e. Windows 7 and later).  The suspended thread is created, and sets its own mask
     * (1, the process mask then), before the process mask is set. */
    T_PLAN(11, "a process mask set after a thread set its own is every thread's mask, a suspended thread's too") {
        HANDLE hs = rt_thread_create(ctx, 0, T_GUEST_ADDR(tc_return_now), 0, CREATE_SUSPENDED, NULL);
        T_REQUIRE(hs != NULL);
        T_EQ(rt_thread_set_affinity(hs, 1), 1);
        rt_dispatcher_lock();
        rt_process_object()->affinity = 3;
        rt_process_object()->affinity_changes++;
        rt_dispatcher_unlock();
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_thread_set_affinity(RT_PSEUDO_THREAD, 1), 3);   /* the main thread's own 1 is stale */
        T_EQ(rt_thread_set_affinity(hs, 1), 3);                 /* and the suspended thread's */
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_EQ(rt_thread_resume(hs), 1);
        T_EQ(rt_wait_handle(ctx, hs, INFINITE, FALSE), WAIT_OBJECT_0);
        T_EQ(rt_handle_close(hs), TRUE);
    }
}

/* Cases 12 and 13 share one blocked thread h (id) and its opened handle h2. */
TEST(core, thread_ctl_open)
{
    Ctx *ctx = T_CTX();
    DWORD id = 0;
    HANDLE h = NULL, h2 = NULL;

    T_PLAN(12, "open finds a running thread by id") {
        tc_go = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(tc_go != NULL);
        wu_store(&tc_started, 0);
        h = rt_thread_create(ctx, 0, T_GUEST_ADDR(tc_wait_for_go), 0, 0, &id);
        T_REQUIRE(h != NULL);
        T_TRUE(EVENTUALLY(wu_load(&tc_started)));
        rt_set_last_error(ctx, SENTINEL);
        h2 = rt_thread_open(id);
        T_REQUIRE(h2 != NULL);
        T_TRUE(h2 != h);
        T_TRUE(wu_obj(h2) == wu_obj(h));
        T_EQ(T_LAST_ERROR(), SENTINEL);

        HANDLE main_h = rt_thread_open(RT_FIRST_THREAD_ID);
        T_REQUIRE(main_h != NULL);
        T_TRUE((void *)wu_obj(main_h) == (void *)RT_THREAD(ctx)->sub[RT_SUB_HANDLE]);
        T_EQ(rt_handle_close(main_h), TRUE);

        rt_set_last_error(ctx, SENTINEL);
        T_TRUE(rt_thread_open(0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(ctx, SENTINEL);
        T_TRUE(rt_thread_open(0x7FFFFFFCu) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }

    T_PLAN(13, "an ended thread is not found, though a handle to it is open") {
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_event_set(ctx, tc_go), TRUE);
        T_EQ(rt_wait_handle(ctx, h2, INFINITE, FALSE), WAIT_OBJECT_0);
        rt_set_last_error(ctx, SENTINEL);
        T_TRUE(rt_thread_open(id) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_TRUE(T_LOGGED(RT_CH_THREAD, "OpenThread: no running thread has id"));
        T_EQ(rt_handle_close(h2), TRUE);
        T_EQ(rt_handle_close(tc_go), TRUE);
    }
}

/* Case 14 has its own blocked thread: it is looked at while running and after it ended. */
TEST(core, thread_ctl_from_handle)
{
    Ctx *ctx = T_CTX();

    T_PLAN(14, "from_handle gives the RtThread and a reference to its object") {
        HANDLE h = NULL;
        RtObject *obj = NULL;
        RtThread *t = NULL;
        RtObject *want = NULL;
        uint64_t refs0 = 0;

        tc_go = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(tc_go != NULL);
        wu_store(&tc_started, 0);
        h = rt_thread_create(ctx, 0, T_GUEST_ADDR(tc_wait_for_go), 0, 0, NULL);
        T_REQUIRE(h != NULL);
        T_TRUE(EVENTUALLY(wu_load(&tc_started)));
        want = (RtObject *)wu_obj(h);
        T_REQUIRE(want != NULL);
        RtThreadObject *o = (RtThreadObject *)want;

        refs0 = want->refs;
        obj = NULL;
        t = rt_thread_from_handle(h, &obj);
        T_TRUE(t == o->thread);
        T_TRUE(obj == want);
        T_EQ(want->refs, refs0 + 1);
        rt_object_unref(obj);
        T_EQ(want->refs, refs0);

        obj = NULL;
        t = rt_thread_from_handle(RT_PSEUDO_THREAD, &obj);
        T_TRUE(t == rt_thread_self());
        T_TRUE(obj != NULL);
        rt_object_unref(obj);

        T_EQ(rt_event_set(ctx, tc_go), TRUE);
        T_EQ(rt_wait_handle(ctx, h, INFINITE, FALSE), WAIT_OBJECT_0);
        obj = NULL;
        t = rt_thread_from_handle(h, &obj);
        T_TRUE(t == o->thread);
        T_TRUE(obj == want);
        rt_object_unref(obj);

        obj = want;
        rt_set_last_error(ctx, SENTINEL);
        T_TRUE(rt_thread_from_handle(NULL, &obj) == NULL);
        T_TRUE(obj == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS(rt_thread_from_handle(h, NULL), "object is NULL");
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(tc_go), TRUE);
    }
}

/* Case 15: eight threads set and read their own priority while main opens them by id. */
#define TC_THREADS 8
#define TC_ROUNDS  2000
static int tc_fails[TC_THREADS];

GUEST_FN(tc_cycle)
{
    static const int vals[7] = { -15, -2, -1, 0, 1, 2, 15 };
    unsigned i = (unsigned)ctx->r[1];
    int fails = 0;
    for (unsigned round = 0; round < TC_ROUNDS; round++) {
        int v = vals[round % 7];
        if (!rt_thread_set_priority(RT_PSEUDO_THREAD, v)) fails++;
        if (rt_thread_get_priority(RT_PSEUDO_THREAD) != v) fails++;
    }
    tc_fails[i] = fails;
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

TEST(core, thread_ctl_concurrent)
{
    Ctx *ctx = T_CTX();

    T_PLAN(15, "priority under concurrent opens: every open is a handle or invalid parameter") {
        HANDLE h[TC_THREADS];
        DWORD id[TC_THREADS];
        for (unsigned i = 0; i < TC_THREADS; i++) {
            h[i] = rt_thread_create(ctx, 0, T_GUEST_ADDR(tc_cycle), i, 0, &id[i]);
            T_REQUIRE(h[i] != NULL);
        }
        for (;;) {
            bool all = rt_wait_handles(ctx, TC_THREADS, h, TRUE, 0, FALSE) == WAIT_OBJECT_0;
            for (unsigned i = 0; i < TC_THREADS; i++) {
                rt_set_last_error(ctx, SENTINEL);
                HANDLE o = rt_thread_open(id[i]);
                if (o != NULL) {
                    T_EQ(rt_handle_close(o), TRUE);
                } else {
                    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
                }
            }
            if (all) break;
        }
        T_EQ(rt_wait_handles(ctx, TC_THREADS, h, TRUE, INFINITE, FALSE), WAIT_OBJECT_0);
        for (unsigned i = 0; i < TC_THREADS; i++) {
            T_EQ(tc_fails[i], 0);
            T_EQ(rt_handle_close(h[i]), TRUE);
        }
    }
}
