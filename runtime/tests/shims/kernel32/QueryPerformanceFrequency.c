#include <string.h>

#include "rt/test.h"

/* The value is the design's 10 MHz, which is the rate QueryPerformanceCounter counts at.
 * The Microsoft page does not state the number, so the test pins the design value. */
TEST(kernel32, QueryPerformanceFrequency)
{
    LARGE_INTEGER f, g;

    T_CASE("writes 10000000 counts a second and returns TRUE");
    memset(&f, 0xAB, sizeof f);
    T_EQ(CALL(kernel32, QueryPerformanceFrequency, &f), TRUE);
    T_EQ(f.QuadPart, 10000000);

    T_CASE("the same value on every call");
    T_EQ(CALL(kernel32, QueryPerformanceFrequency, &g), TRUE);
    T_EQ(g.QuadPart, f.QuadPart);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, QueryPerformanceFrequency, &f);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, QueryPerformanceFrequency, NULL), "NULL lpFrequency");
}
