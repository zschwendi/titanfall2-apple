#include "rt/test.h"
#include "rt/sync.h"
#include "rt/thread.h"
#include "rt/time.h"

/* A guest APC: records each run's parameter, in order.  Sleep is not alertable, so the test
 * checks that a queued call is still pending after Sleep and runs at the next alertable wait. */
static unsigned apc_runs;
static uint64_t apc_param;

GUEST_FN(record_apc)
{
    apc_runs++;
    apc_param = ctx->r[1];
    GUEST_RET(ctx);
}

/* The oracle is the core's clock, read either side of the call. */
TEST(kernel32, Sleep)
{
    uint64_t t0, t1;

    T_CASE("sleeps the given number of milliseconds");
    t0 = rt_time_mono_100ns();
    CALL(kernel32, Sleep, 30);
    t1 = rt_time_mono_100ns();
    T_TRUE(t1 - t0 >= 30 * 10000ull);               /* 30 ms, in 100 ns units */
    T_TRUE(t1 - t0 < 5 * 10000000ull);              /* well under 5 s */

    T_CASE("zero gives up the processor and returns at once");
    /* The fastest of 50 calls: on a busy machine one call can be descheduled for longer than
     * any bound worth asserting, but a Sleep(0) that waited for a timer would never be fast. */
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < 50; i++) {
        t0 = rt_time_mono_100ns();
        CALL(kernel32, Sleep, 0);
        t1 = rt_time_mono_100ns();
        if (t1 - t0 < best) best = t1 - t0;
    }
    T_TRUE(best < 10 * 10000ull);                   /* under 10 ms: no timer tick is waited for */

    T_CASE("is not alertable, so a queued APC does not run");
    apc_runs = 0;
    T_EQ(rt_apc_queue(T_CTX(), RT_THREAD(T_CTX()), T_GUEST_ADDR(record_apc), 5), TRUE);
    CALL(kernel32, Sleep, 30);
    CALL(kernel32, Sleep, 0);
    T_EQ(apc_runs, 0);
    T_EQ(rt_sleep_ex(T_CTX(), 0, TRUE), WAIT_IO_COMPLETION);       /* the next alertable wait runs it */
    T_EQ(apc_runs, 1);
    T_EQ(apc_param, 5);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, Sleep, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
