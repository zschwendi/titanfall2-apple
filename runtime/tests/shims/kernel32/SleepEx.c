#include "rt/test.h"
#include "rt/sync.h"
#include "rt/thread.h"
#include "rt/time.h"

/* Guest APCs: each run records its parameter, in order. */
static unsigned apc_runs;
static uint64_t apc_seen[4];

GUEST_FN(record_apc)
{
    if (apc_runs < 4) apc_seen[apc_runs] = ctx->r[1];
    apc_runs++;
    GUEST_RET(ctx);
}

/* Milliseconds in 100 ns units, the core's clock. */
#define MS(n)  ((uint64_t)(n) * 10000u)

/* The oracle is the core's clock, read either side of the call.  APCs are queued to the test's
 * own thread, which is the thread that runs them in an alertable wait. */
TEST(kernel32, SleepEx)
{
    uint64_t t0, t1;

    T_CASE("returns 0 when the time has passed");
    T_EQ(CALL(kernel32, SleepEx, 10, FALSE), 0);
    T_EQ(CALL(kernel32, SleepEx, 10, TRUE), 0);

    T_CASE("sleeps the given number of milliseconds");
    t0 = rt_time_mono_100ns();
    CALL(kernel32, SleepEx, 30, FALSE);
    t1 = rt_time_mono_100ns();
    T_TRUE(t1 - t0 >= MS(30));
    T_TRUE(t1 - t0 < MS(5000));                     /* well under 5 s */
    t0 = rt_time_mono_100ns();
    CALL(kernel32, SleepEx, 30, TRUE);
    t1 = rt_time_mono_100ns();
    T_TRUE(t1 - t0 >= MS(30));
    T_TRUE(t1 - t0 < MS(5000));

    T_CASE("zero gives up the processor and returns at once");
    /* The fastest of 50 calls: on a busy machine one call can be descheduled for longer than
     * any bound worth asserting, but a zero sleep that waited for a timer would never be fast. */
    for (int alertable = 0; alertable < 2; alertable++) {
        uint64_t best = UINT64_MAX;
        for (int i = 0; i < 50; i++) {
            t0 = rt_time_mono_100ns();
            T_EQ(CALL(kernel32, SleepEx, 0, alertable), 0);
            t1 = rt_time_mono_100ns();
            if (t1 - t0 < best) best = t1 - t0;
        }
        T_TRUE(best < MS(10));                      /* no timer tick is waited for */
    }

    T_CASE("alertable: queued APCs run in order and the call returns WAIT_IO_COMPLETION");
    apc_runs = 0;
    T_EQ(rt_apc_queue(T_CTX(), RT_THREAD(T_CTX()), T_GUEST_ADDR(record_apc), 7), TRUE);
    T_EQ(rt_apc_queue(T_CTX(), RT_THREAD(T_CTX()), T_GUEST_ADDR(record_apc), 8), TRUE);
    t0 = rt_time_mono_100ns();
    T_EQ(CALL(kernel32, SleepEx, 5000, TRUE), WAIT_IO_COMPLETION);
    t1 = rt_time_mono_100ns();
    T_EQ(apc_runs, 2);
    T_EQ(apc_seen[0], 7);
    T_EQ(apc_seen[1], 8);
    T_TRUE(t1 - t0 < MS(1000));                     /* it returned at once, not after the 5 s */

    T_CASE("a queued APC runs before sleeping, even with a time of 0");
    apc_runs = 0;
    T_EQ(rt_apc_queue(T_CTX(), RT_THREAD(T_CTX()), T_GUEST_ADDR(record_apc), 10), TRUE);
    T_EQ(CALL(kernel32, SleepEx, 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_runs, 1);
    T_EQ(apc_seen[0], 10);

    T_CASE("not alertable: a queued APC does not run");
    apc_runs = 0;
    T_EQ(rt_apc_queue(T_CTX(), RT_THREAD(T_CTX()), T_GUEST_ADDR(record_apc), 9), TRUE);
    T_EQ(CALL(kernel32, SleepEx, 30, FALSE), 0);
    T_EQ(apc_runs, 0);
    T_EQ(CALL(kernel32, SleepEx, 0, TRUE), WAIT_IO_COMPLETION);     /* the next alertable wait runs it */
    T_EQ(apc_runs, 1);
    T_EQ(apc_seen[0], 9);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, SleepEx, 0, FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SleepEx, 0, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
