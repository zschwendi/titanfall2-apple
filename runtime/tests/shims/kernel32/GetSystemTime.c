#include <string.h>

#include "rt/test.h"
#include "rt/time.h"

/* 2000-01-01 00:00:00.123 UTC, a Saturday. */
static uint64_t fixed_clock(void)
{
    return rt_time_filetime_from_unix(946684800, 0) + 1234567;
}

TEST(kernel32, GetSystemTime)
{
    SYSTEMTIME st;

    rt_time_set_source(NULL, fixed_clock);

    T_CASE("writes the UTC wall clock as SYSTEMTIME, all eight members");
    memset(&st, 0xFF, sizeof st);
    CALL(kernel32, GetSystemTime, &st);
    T_EQ(st.wYear, 2000);
    T_EQ(st.wMonth, 1);
    T_EQ(st.wDayOfWeek, 6);
    T_EQ(st.wDay, 1);
    T_EQ(st.wHour, 0);
    T_EQ(st.wMinute, 0);
    T_EQ(st.wSecond, 0);
    T_EQ(st.wMilliseconds, 123);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetSystemTime, &st);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, GetSystemTime, NULL), "NULL lpSystemTime");

    rt_time_set_source(NULL, NULL);
}
