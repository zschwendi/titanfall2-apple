#include <string.h>

#include "rt/test.h"
#include "rt/time.h"

/* The expected values are fixed calendar dates, converted to FILETIME by the core's
 * rt_time_filetime_from_unix (checked against its literal value too). */
TEST(kernel32, FileTimeToSystemTime)
{
    FILETIME ft;
    SYSTEMTIME st, before;
    uint64_t v;

    T_CASE("converts a FILETIME to every member of SYSTEMTIME");
    v = rt_time_filetime_from_unix(946684800, 0);       /* 2000-01-01 00:00:00 UTC */
    T_EQ(v, UINT64_C(125911584000000000));
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
    memset(&st, 0xAB, sizeof st);
    T_EQ(CALL(kernel32, FileTimeToSystemTime, &ft, &st), TRUE);
    T_EQ(st.wYear, 2000);
    T_EQ(st.wMonth, 1);
    T_EQ(st.wDayOfWeek, 6);                             /* Saturday */
    T_EQ(st.wDay, 1);
    T_EQ(st.wHour, 0);
    T_EQ(st.wMinute, 0);
    T_EQ(st.wSecond, 0);
    T_EQ(st.wMilliseconds, 0);

    T_CASE("milliseconds from the sub-second part");
    v += 1234567;                                       /* 0.1234567 s */
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
    memset(&st, 0xAB, sizeof st);
    T_EQ(CALL(kernel32, FileTimeToSystemTime, &ft, &st), TRUE);
    T_EQ(st.wYear, 2000);
    T_EQ(st.wMonth, 1);
    T_EQ(st.wDayOfWeek, 6);
    T_EQ(st.wDay, 1);
    T_EQ(st.wHour, 0);
    T_EQ(st.wMinute, 0);
    T_EQ(st.wSecond, 0);
    T_EQ(st.wMilliseconds, 123);

    T_CASE("value 0 is 1601-01-01, a Monday");
    ft.dwLowDateTime = 0;
    ft.dwHighDateTime = 0;
    memset(&st, 0xAB, sizeof st);
    T_EQ(CALL(kernel32, FileTimeToSystemTime, &ft, &st), TRUE);
    T_EQ(st.wYear, 1601);
    T_EQ(st.wMonth, 1);
    T_EQ(st.wDayOfWeek, 1);                             /* Monday */
    T_EQ(st.wDay, 1);
    T_EQ(st.wHour, 0);
    T_EQ(st.wMinute, 0);
    T_EQ(st.wSecond, 0);
    T_EQ(st.wMilliseconds, 0);

    T_CASE("a value of 0x8000000000000000 or more fails and leaves the output alone");
    ft.dwLowDateTime = 0;
    ft.dwHighDateTime = 0x80000000u;
    memset(&st, 0xAB, sizeof st);
    before = st;
    T_EQ(CALL(kernel32, FileTimeToSystemTime, &ft, &st), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(memcmp(&st, &before, sizeof st), 0);
    ft.dwLowDateTime = 0xFFFFFFFFu;
    ft.dwHighDateTime = 0xFFFFFFFFu;
    T_EQ(CALL(kernel32, FileTimeToSystemTime, &ft, &st), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(memcmp(&st, &before, sizeof st), 0);

    T_CASE("does not change last error on success");
    ft.dwLowDateTime = 0;
    ft.dwHighDateTime = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FileTimeToSystemTime, &ft, &st), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, FileTimeToSystemTime, NULL, &st), "NULL lpFileTime");
    T_TRAPS(CALL(kernel32, FileTimeToSystemTime, &ft, NULL), "NULL lpSystemTime");
}
