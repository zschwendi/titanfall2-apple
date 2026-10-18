#include <string.h>

#include "rt/test.h"
#include "rt/time.h"

#define UNITS_PER_SEC  UINT64_C(10000000)

/* The oracle is the core's wall clock, read either side of the call.  The wall clock may be
 * stepped by the host, so the checks allow one second around the reads rather than being
 * exact to the unit; a shim that split the value wrongly would be off by far more. */
TEST(kernel32, GetSystemTimeAsFileTime)
{
    FILETIME ft;
    uint64_t u0, u1, v;

    T_CASE("writes the UTC wall clock as a FILETIME");
    memset(&ft, 0xAB, sizeof ft);
    u0 = rt_time_utc_100ns();
    CALL(kernel32, GetSystemTimeAsFileTime, &ft);
    u1 = rt_time_utc_100ns();
    v = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    T_TRUE(v + UNITS_PER_SEC >= u0 && v <= u1 + UNITS_PER_SEC);
    T_TRUE(v >= UINT64_C(132223104000000000) && v <= UINT64_C(157469184000000000));  /* 2020 to 2100 */

    T_CASE("both halves of the FILETIME are written");
    memset(&ft, 0xFF, sizeof ft);
    u0 = rt_time_utc_100ns();
    CALL(kernel32, GetSystemTimeAsFileTime, &ft);
    u1 = rt_time_utc_100ns();
    T_TRUE(ft.dwHighDateTime != 0xFFFFFFFFu);
    T_TRUE(ft.dwLowDateTime != 0xFFFFFFFFu);
    v = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    T_TRUE(v + UNITS_PER_SEC >= u0 && v <= u1 + UNITS_PER_SEC);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetSystemTimeAsFileTime, &ft);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, GetSystemTimeAsFileTime, NULL), "NULL lpSystemTimeAsFileTime");
}
