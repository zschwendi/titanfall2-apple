#include <string.h>

#include "rt/test.h"
#include "rt/time.h"

/* The oracle for the zone-less call is the core's own host bias: the NULL zone must give what
 * a zone with that Bias gives.  The other expected values are worked out by hand from the
 * Bias (local = UTC - Bias minutes) and the weekday of the date. */
TEST(kernel32, SystemTimeToTzSpecificLocalTime)
{
    TIME_ZONE_INFORMATION tzi;
    SYSTEMTIME utc, local, local2, before;

    T_CASE("converts a universal time with a zone given by the caller");
    memset(&tzi, 0, sizeof tzi);
    tzi.Bias = 300;
    memset(&utc, 0, sizeof utc);
    utc.wYear = 2002; utc.wMonth = 5; utc.wDay = 1; utc.wHour = 12;
    memset(&local, 0xAB, sizeof local);
    T_EQ(CALL(kernel32, SystemTimeToTzSpecificLocalTime, &tzi, &utc, &local), TRUE);
    T_EQ(local.wYear, 2002);
    T_EQ(local.wMonth, 5);
    T_EQ(local.wDay, 1);
    T_EQ(local.wHour, 7);
    T_EQ(local.wMinute, 0);
    T_EQ(local.wSecond, 0);
    T_EQ(local.wMilliseconds, 0);
    T_EQ(local.wDayOfWeek, 3);                       /* 2002-05-01 was a Wednesday */

    tzi.Bias = -60;
    memset(&utc, 0, sizeof utc);
    utc.wYear = 2001; utc.wMonth = 12; utc.wDay = 31; utc.wHour = 23; utc.wMinute = 30;
    memset(&local, 0xAB, sizeof local);
    T_EQ(CALL(kernel32, SystemTimeToTzSpecificLocalTime, &tzi, &utc, &local), TRUE);
    T_EQ(local.wYear, 2002);
    T_EQ(local.wMonth, 1);
    T_EQ(local.wDay, 1);
    T_EQ(local.wHour, 0);
    T_EQ(local.wMinute, 30);
    T_EQ(local.wSecond, 0);
    T_EQ(local.wMilliseconds, 0);
    T_EQ(local.wDayOfWeek, 2);                       /* 2002-01-01 was a Tuesday */

    T_CASE("uses the active zone when the zone is NULL");
    memset(&tzi, 0, sizeof tzi);
    tzi.Bias = rt_time_zone_bias_minutes();
    memset(&utc, 0, sizeof utc);
    utc.wYear = 2002; utc.wMonth = 5; utc.wDay = 1; utc.wHour = 12;
    memset(&local, 0xAB, sizeof local);
    memset(&local2, 0xCD, sizeof local2);
    T_EQ(CALL(kernel32, SystemTimeToTzSpecificLocalTime, &tzi, &utc, &local), TRUE);
    T_EQ(CALL(kernel32, SystemTimeToTzSpecificLocalTime, NULL, &utc, &local2), TRUE);
    T_TRUE(memcmp(&local, &local2, sizeof local) == 0);

    T_CASE("refuses an invalid universal time and writes nothing");
    memset(&utc, 0, sizeof utc);
    utc.wYear = 2002; utc.wMonth = 13; utc.wDay = 1;   /* month 13 */
    memset(&local, 0xAB, sizeof local);
    before = local;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SystemTimeToTzSpecificLocalTime, NULL, &utc, &local), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(memcmp(&local, &before, sizeof local) == 0);

    T_CASE("success leaves last error alone");
    memset(&tzi, 0, sizeof tzi);
    tzi.Bias = 300;
    memset(&utc, 0, sizeof utc);
    utc.wYear = 2002; utc.wMonth = 5; utc.wDay = 1; utc.wHour = 12;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SystemTimeToTzSpecificLocalTime, &tzi, &utc, &local), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* the stops: a zone with a daylight rule, each NULL pointer, a local time out of range */
    memset(&tzi, 0, sizeof tzi);
    tzi.StandardDate.wMonth = 10;
    T_TRAPS(CALL(kernel32, SystemTimeToTzSpecificLocalTime, &tzi, &utc, &local),
            "zone with a daylight rule");
    T_TRAPS(CALL(kernel32, SystemTimeToTzSpecificLocalTime, NULL, NULL, &local), "NULL lpUniversalTime");
    T_TRAPS(CALL(kernel32, SystemTimeToTzSpecificLocalTime, NULL, &utc, NULL), "NULL lpLocalTime");
    memset(&tzi, 0, sizeof tzi);
    tzi.Bias = 60;
    memset(&utc, 0, sizeof utc);
    utc.wYear = 1601; utc.wMonth = 1; utc.wDay = 1;   /* 1600-12-31 23:00 local */
    T_TRAPS(CALL(kernel32, SystemTimeToTzSpecificLocalTime, &tzi, &utc, &local),
            "local time before 1601-01-01 or after 30827-12-31");
    tzi.Bias = -60;
    memset(&utc, 0, sizeof utc);
    utc.wYear = 30827; utc.wMonth = 12; utc.wDay = 31; utc.wHour = 23; utc.wMinute = 30;   /* 30828 local */
    T_TRAPS(CALL(kernel32, SystemTimeToTzSpecificLocalTime, &tzi, &utc, &local),
            "local time before 1601-01-01 or after 30827-12-31");
}
