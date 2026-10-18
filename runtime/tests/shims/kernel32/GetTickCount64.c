#include <unistd.h>

#include "rt/test.h"
#include "rt/time.h"

/* The oracle is the core's tick count read either side of the call.  The shim returns that
 * count unchanged, so the value must lie between the two reads exactly. */
TEST(kernel32, GetTickCount64)
{
    uint64_t t0, t1, v, v0, v1, prev;
    unsigned back = 0;

    T_CASE("returns the millisecond tick count as a 64-bit value");
    t0 = rt_time_tick_ms();
    v = CALL(kernel32, GetTickCount64);
    t1 = rt_time_tick_ms();
    T_TRUE(v >= t0 && v <= t1);
    T_TRUE(v >= 600000);                /* ten minutes plus the time since start-up */

    T_CASE("counts milliseconds");
    v0 = CALL(kernel32, GetTickCount64);
    usleep(100000);
    v1 = CALL(kernel32, GetTickCount64);
    T_TRUE(v1 - v0 >= 99 && v1 - v0 < 1000);

    T_CASE("two calls in a row never go backwards");
    prev = CALL(kernel32, GetTickCount64);
    for (unsigned i = 0; i < 10000; i++) {
        v = CALL(kernel32, GetTickCount64);
        if (v < prev) back++;
        prev = v;
    }
    T_EQ(back, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetTickCount64);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
