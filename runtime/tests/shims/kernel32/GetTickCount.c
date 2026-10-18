#include <unistd.h>

#include "rt/test.h"
#include "rt/time.h"

/* The oracle is the core's tick count read either side of the call.  The shim's value is that
 * count modulo 2^32 at some instant between the two reads, which is checked with modular
 * arithmetic so that a wrap of the DWORD is not mistaken for a backwards step. */
TEST(kernel32, GetTickCount)
{
    uint64_t t0, t1;
    DWORD v, v0, v1, prev;
    unsigned back = 0;

    T_CASE("returns the low 32 bits of the millisecond tick count");
    t0 = rt_time_tick_ms();
    v = CALL(kernel32, GetTickCount);
    t1 = rt_time_tick_ms();
    T_TRUE((DWORD)(v - (DWORD)t0) <= (DWORD)(t1 - t0));
    T_TRUE(v >= 600000);                /* ten minutes plus the time since start-up */

    T_CASE("counts milliseconds");
    v0 = CALL(kernel32, GetTickCount);
    usleep(100000);
    v1 = CALL(kernel32, GetTickCount);
    T_TRUE((DWORD)(v1 - v0) >= 99 && (DWORD)(v1 - v0) < 1000);

    T_CASE("two calls in a row never go backwards");
    prev = CALL(kernel32, GetTickCount);
    for (unsigned i = 0; i < 10000; i++) {
        v = CALL(kernel32, GetTickCount);
        if ((DWORD)(v - prev) >= 0x80000000u) back++;      /* a step backwards, modulo 2^32 */
        prev = v;
    }
    T_EQ(back, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetTickCount);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
