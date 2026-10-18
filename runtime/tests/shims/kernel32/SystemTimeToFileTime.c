#include <string.h>

#include "rt/test.h"

static void set_st(SYSTEMTIME *st, WORD y, WORD mo, WORD d, WORD h, WORD mi, WORD s, WORD ms)
{
    memset(st, 0, sizeof *st);
    st->wYear = y;
    st->wMonth = mo;
    st->wDay = d;
    st->wHour = h;
    st->wMinute = mi;
    st->wSecond = s;
    st->wMilliseconds = ms;
}

/* An invalid time: the call fails with ERROR_INVALID_PARAMETER (set by the core) and writes
 * nothing, so a FILETIME preset to 0xAB keeps every byte. */
static void expect_refused(const SYSTEMTIME *st)
{
    FILETIME ft;

    memset(&ft, 0xAB, sizeof ft);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, SystemTimeToFileTime, st, &ft), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(ft.dwLowDateTime, 0xABABABABu);
    T_EQ(ft.dwHighDateTime, 0xABABABABu);
}

TEST(kernel32, SystemTimeToFileTime)
{
    SYSTEMTIME st;
    FILETIME ft;

    T_CASE("converts a SYSTEMTIME to a FILETIME, both halves written");
    set_st(&st, 1601, 1, 1, 0, 0, 0, 0);
    memset(&ft, 0xAB, sizeof ft);
    T_EQ(CALL(kernel32, SystemTimeToFileTime, &st, &ft), TRUE);
    T_EQ(ft.dwLowDateTime, 0u);
    T_EQ(ft.dwHighDateTime, 0u);
    set_st(&st, 1980, 1, 1, 0, 0, 0, 0);
    memset(&ft, 0xAB, sizeof ft);
    T_EQ(CALL(kernel32, SystemTimeToFileTime, &st, &ft), TRUE);
    T_EQ(ft.dwLowDateTime, 0xE1D58000u);
    T_EQ(ft.dwHighDateTime, 0x01A8E79Fu);
    set_st(&st, 2002, 7, 26, 11, 55, 32, 123);
    memset(&ft, 0xAB, sizeof ft);
    T_EQ(CALL(kernel32, SystemTimeToFileTime, &st, &ft), TRUE);
    T_EQ(ft.dwLowDateTime, 0x580716B0u);
    T_EQ(ft.dwHighDateTime, 0x01C2349Bu);

    T_CASE("refuses an invalid time with ERROR_INVALID_PARAMETER and writes nothing");
    set_st(&st, 1600, 12, 31, 0, 0, 0, 0);           /* before 1601 */
    expect_refused(&st);
    set_st(&st, 2002, 0, 26, 11, 55, 32, 123);       /* month 0 */
    expect_refused(&st);
    set_st(&st, 2002, 7, 26, 24, 0, 0, 0);           /* hour 24 */
    expect_refused(&st);
    set_st(&st, 2002, 7, 26, 11, 60, 0, 0);          /* minute 60 */
    expect_refused(&st);

    T_CASE("ignores wDayOfWeek");
    set_st(&st, 2002, 7, 26, 11, 55, 32, 123);
    st.wDayOfWeek = 0;
    memset(&ft, 0xAB, sizeof ft);
    T_EQ(CALL(kernel32, SystemTimeToFileTime, &st, &ft), TRUE);
    T_EQ(ft.dwLowDateTime, 0x580716B0u);
    T_EQ(ft.dwHighDateTime, 0x01C2349Bu);
    st.wDayOfWeek = 6;
    memset(&ft, 0xAB, sizeof ft);
    T_EQ(CALL(kernel32, SystemTimeToFileTime, &st, &ft), TRUE);
    T_EQ(ft.dwLowDateTime, 0x580716B0u);
    T_EQ(ft.dwHighDateTime, 0x01C2349Bu);

    T_CASE("does not change last error on success");
    set_st(&st, 2002, 7, 26, 11, 55, 32, 123);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SystemTimeToFileTime, &st, &ft), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The NULL stops: the page does not say what happens, so the shim stops (RT-TRAPS). */
    T_TRAPS(CALL(kernel32, SystemTimeToFileTime, NULL, &ft), "NULL lpSystemTime");
    T_TRAPS(CALL(kernel32, SystemTimeToFileTime, &st, NULL), "NULL lpFileTime");
}
