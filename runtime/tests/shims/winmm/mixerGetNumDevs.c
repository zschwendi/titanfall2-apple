#include "rt/test.h"

TEST(winmm, mixerGetNumDevs)
{
    UINT n;

    T_CASE("returns 0 when there is no mixer device");
    n = CALL(winmm, mixerGetNumDevs);
    T_EQ(n, 0);

    T_CASE("returns 0 on the second of two calls in a row");
    CALL(winmm, mixerGetNumDevs);
    n = CALL(winmm, mixerGetNumDevs);
    T_EQ(n, 0);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    n = CALL(winmm, mixerGetNumDevs);
    T_EQ(n, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
