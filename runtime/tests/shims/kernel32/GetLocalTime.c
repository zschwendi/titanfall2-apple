#include <string.h>

#include "rt/test.h"
#include "rt/locale.h"
#include "rt/time.h"

/* 2000-01-01 00:00:00.123 UTC, a Saturday. */
static uint64_t fixed_clock(void)
{
    return rt_time_filetime_from_unix(946684800, 0) + 1234567;
}

TEST(kernel32, GetLocalTime)
{
    SYSTEMTIME lt;
    RtSystemTime want;
    LONG bias;
    uint64_t local;

    rt_time_set_source(NULL, fixed_clock);

    /* The expected value is the core's conversion of the shifted clock: UTC = local + bias. */
    bias = rt_time_zone_bias_minutes();
    local = fixed_clock() - (uint64_t)((int64_t)bias * 600000000);
    rt_systemtime_from_filetime(local, &want);

    T_CASE("writes the local wall clock as SYSTEMTIME, all eight members");
    memset(&lt, 0xFF, sizeof lt);
    CALL(kernel32, GetLocalTime, &lt);
    T_EQ(lt.wYear, want.wYear);
    T_EQ(lt.wMonth, want.wMonth);
    T_EQ(lt.wDayOfWeek, want.wDayOfWeek);
    T_EQ(lt.wDay, want.wDay);
    T_EQ(lt.wHour, want.wHour);
    T_EQ(lt.wMinute, want.wMinute);
    T_EQ(lt.wSecond, want.wSecond);
    T_EQ(lt.wMilliseconds, want.wMilliseconds);
    /* Whatever the zone, a whole-minute shift leaves the seconds and milliseconds alone. */
    T_EQ(lt.wSecond, 0);
    T_EQ(lt.wMilliseconds, 123);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetLocalTime, &lt);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, GetLocalTime, NULL), "NULL lpSystemTime");

    rt_time_set_source(NULL, NULL);
}
