#include <string.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/time.h"

/* The core's clock is the oracle: each count the shim writes must lie between two reads of
 * rt_time_mono_100ns() taken either side of the call.  The 100 ns unit is checked against a
 * 50 ms sleep, which the clock must count as at least 500000 units. */
TEST(kernel32, QueryPerformanceCounter)
{
    LARGE_INTEGER q;
    uint64_t m0, m1, v, a, b, prev;
    unsigned back = 0;

    T_CASE("writes the count and returns TRUE");
    memset(&q, 0xAB, sizeof q);
    m0 = rt_time_mono_100ns();
    T_EQ(CALL(kernel32, QueryPerformanceCounter, &q), TRUE);
    m1 = rt_time_mono_100ns();
    v = (uint64_t)q.QuadPart;
    T_TRUE(v >= m0 && v <= m1);

    T_CASE("the count is in 100 ns units");
    CALL(kernel32, QueryPerformanceCounter, &q);
    a = (uint64_t)q.QuadPart;
    usleep(50000);
    CALL(kernel32, QueryPerformanceCounter, &q);
    b = (uint64_t)q.QuadPart;
    T_TRUE(b - a >= 500000);            /* 50 ms at 10 MHz: a sleep lasts at least its time */
    T_TRUE(b - a < 100000000);          /* and is not seconds: under 10 s */

    T_CASE("two calls in a row never go backwards");
    prev = 0;
    for (unsigned i = 0; i < 10000; i++) {
        CALL(kernel32, QueryPerformanceCounter, &q);
        v = (uint64_t)q.QuadPart;
        if (v < prev) back++;
        prev = v;
    }
    T_EQ(back, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, QueryPerformanceCounter, &q);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, QueryPerformanceCounter, NULL), "NULL lpPerformanceCount");
}
