#include "rt/test.h"
#include "rt/sync.h"
#include "rt/thread.h"
#include "rt/time.h"

/* A guest APC: records each run's parameter, in order.  WaitForSingleObject is not alertable,
 * so the test checks that a queued call is still pending after the wait and runs at the next
 * alertable wait. */
static unsigned apc_runs;
static uint64_t apc_param;

GUEST_FN(record_apc)
{
    apc_runs++;
    apc_param = ctx->r[1];
    GUEST_RET(ctx);
}

/* The objects are made by the core, on the test's own thread; every wait is a guest call. */
TEST(kernel32, WaitForSingleObject)
{
    uint64_t t0, t1;
    HANDLE ev_set   = rt_event_create(T_CTX(), TRUE, TRUE, NULL);     /* manual-reset, set */
    HANDLE ev_reset = rt_event_create(T_CTX(), TRUE, FALSE, NULL);    /* manual-reset, not set */
    HANDLE sem      = rt_semaphore_create(T_CTX(), 2, 2, NULL);
    HANDLE mtx      = rt_mutex_create(T_CTX(), FALSE, NULL);
    HANDLE closed   = rt_event_create(T_CTX(), TRUE, TRUE, NULL);
    T_REQUIRE(ev_set && ev_reset && sem && mtx && closed);

    T_CASE("waits for a signaled event and returns WAIT_OBJECT_0");
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_set, 0), WAIT_OBJECT_0);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_set, INFINITE), WAIT_OBJECT_0);

    T_CASE("zero timeout on a reset event returns WAIT_TIMEOUT at once");
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_reset, 0), WAIT_TIMEOUT);

    T_CASE("timeout of 20 ms on a reset event returns WAIT_TIMEOUT after 20 ms");
    t0 = rt_time_mono_100ns();
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_reset, 20), WAIT_TIMEOUT);
    t1 = rt_time_mono_100ns();
    T_TRUE(t1 - t0 >= 20 * 10000ull);               /* 20 ms, in 100 ns units */

    T_CASE("each wait on a semaphore takes one count");
    T_EQ(CALL(kernel32, WaitForSingleObject, sem, 0), WAIT_OBJECT_0);     /* count 2 -> 1 */
    T_EQ(CALL(kernel32, WaitForSingleObject, sem, 0), WAIT_OBJECT_0);     /* count 1 -> 0 */
    T_EQ(CALL(kernel32, WaitForSingleObject, sem, 0), WAIT_TIMEOUT);      /* count 0: nothing taken */

    T_CASE("a mutex taken twice by the same thread returns WAIT_OBJECT_0 both times");
    T_EQ(CALL(kernel32, WaitForSingleObject, mtx, 0), WAIT_OBJECT_0);
    T_EQ(CALL(kernel32, WaitForSingleObject, mtx, 0), WAIT_OBJECT_0);

    T_CASE("a handle that is not open fails with WAIT_FAILED and ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, CloseHandle, closed), TRUE);
    T_EQ(CALL(kernel32, WaitForSingleObject, closed, 0), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("is not alertable, so a queued APC does not run");
    apc_runs = 0;
    T_EQ(rt_apc_queue(T_CTX(), RT_THREAD(T_CTX()), T_GUEST_ADDR(record_apc), 7), TRUE);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_set, 0), WAIT_OBJECT_0);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_reset, 0), WAIT_TIMEOUT);
    T_EQ(apc_runs, 0);
    T_EQ(rt_sleep_ex(T_CTX(), 0, TRUE), WAIT_IO_COMPLETION);       /* the next alertable wait runs it */
    T_EQ(apc_runs, 1);
    T_EQ(apc_param, 7);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_set, 0), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev_reset, 0), WAIT_TIMEOUT);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
