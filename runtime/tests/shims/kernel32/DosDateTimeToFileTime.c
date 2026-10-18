#include <string.h>

#include "rt/test.h"

/* A date or time that does not exist: the call fails with ERROR_INVALID_PARAMETER (set by the
 * core) and writes nothing, so a FILETIME preset to 0xAB keeps every byte. */
static void expect_refused(WORD date, WORD time)
{
    FILETIME ft;

    memset(&ft, 0xAB, sizeof ft);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, DosDateTimeToFileTime, date, time, &ft), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(ft.dwLowDateTime, 0xABABABABu);
    T_EQ(ft.dwHighDateTime, 0xABABABABu);
}

TEST(kernel32, DosDateTimeToFileTime)
{
    FILETIME ft;

    T_CASE("converts an MS-DOS date and time to a FILETIME, both halves written");
    memset(&ft, 0xAB, sizeof ft);
    T_EQ(CALL(kernel32, DosDateTimeToFileTime, 0x0021, 0x0000, &ft), TRUE);     /* 1980-01-01 00:00:00 */
    T_EQ(ft.dwLowDateTime, 0xE1D58000u);
    T_EQ(ft.dwHighDateTime, 0x01A8E79Fu);
    memset(&ft, 0xAB, sizeof ft);
    T_EQ(CALL(kernel32, DosDateTimeToFileTime, 0x2B29, 0x0DD4, &ft), TRUE);     /* 2001-09-09 01:46:40 */
    T_EQ(ft.dwLowDateTime, 0x44FF8000u);
    T_EQ(ft.dwHighDateTime, 0x01C138D1u);

    T_CASE("refuses a date or time that does not exist with ERROR_INVALID_PARAMETER and writes nothing");
    expect_refused(0x0001, 0x0000);     /* month 0 */
    expect_refused(0x0020, 0x0000);     /* day 0 */
    expect_refused(0x0021, 0xC000);     /* hour 24 */
    expect_refused(0x0021, 0x001E);     /* seconds divided by 2 = 30, that is 60 seconds */

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, DosDateTimeToFileTime, 0x0021, 0x0000, &ft), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The NULL stop: the page does not say what happens, so the shim stops (RT-TRAPS). */
    T_TRAPS(CALL(kernel32, DosDateTimeToFileTime, 0x0021, 0x0000, NULL), "NULL lpFileTime");
}
