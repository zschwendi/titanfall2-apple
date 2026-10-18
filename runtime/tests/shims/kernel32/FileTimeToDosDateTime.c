#include "rt/test.h"
#include "rt/time.h"

/* The MS-DOS range as FILETIMEs (1980-01-01 00:00:00 and 2108-01-01 00:00:00 UTC).  The
 * shim has the same two constants; these checks tie them to the core's arithmetic. */
#define FT_DOS_FIRST  UINT64_C(119600064000000000)
#define FT_DOS_END    UINT64_C(159992928000000000)

static FILETIME filetime(uint64_t t)
{
    FILETIME f;
    f.dwLowDateTime = (DWORD)t;
    f.dwHighDateTime = (DWORD)(t >> 32);
    return f;
}

TEST(kernel32, FileTimeToDosDateTime)
{
    FILETIME ft;
    WORD d, t;

    T_CASE("converts a file time to the MS-DOS date and time");
    ft = filetime(rt_time_filetime_from_unix(946684800, 0));           /* 2000-01-01 00:00:00 */
    d = 0x5A5A; t = 0x5A5A;
    T_EQ(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, &t), TRUE);
    T_EQ(d, 0x2821);
    T_EQ(t, 0);
    ft = filetime(rt_time_filetime_from_unix(1000000000, 0));          /* 2001-09-09 01:46:40 */
    d = 0x5A5A; t = 0x5A5A;
    T_EQ(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, &t), TRUE);
    T_EQ(d, 0x2B29);
    T_EQ(t, 0x0DD4);

    T_CASE("an odd second rounds down");
    ft = filetime(rt_time_filetime_from_unix(1000000001, 0));          /* 01:46:41 */
    d = 0x5A5A; t = 0x5A5A;
    T_EQ(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, &t), TRUE);
    T_EQ(d, 0x2B29);
    T_EQ(t, 0x0DD4);

    T_CASE("the first and last DOS dates convert");
    T_EQ(rt_time_filetime_from_unix(315532800, 0), FT_DOS_FIRST);     /* the constants are right */
    T_EQ(rt_time_filetime_from_unix(4354819200, 0), FT_DOS_END);
    ft = filetime(rt_time_filetime_from_unix(315532800, 0));           /* 1980-01-01 00:00:00 */
    d = 0x5A5A; t = 0x5A5A;
    T_EQ(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, &t), TRUE);
    T_EQ(d, 0x0021);
    T_EQ(t, 0);
    ft = filetime(rt_time_filetime_from_unix(4354819199, 999999900)); /* 2107-12-31 23:59:59.9999999 */
    d = 0x5A5A; t = 0x5A5A;
    T_EQ(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, &t), TRUE);
    T_EQ(d, 0xFF9F);
    T_EQ(t, 0xBF7D);

    T_CASE("a time before 1980 or after 2107 fails and writes neither WORD");
    {
        static const uint64_t outside[] = {
            0,                                /* 1601-01-01 00:00:00 */
            FT_DOS_FIRST - 1,                 /* 1979-12-31 23:59:59.9999999 */
            FT_DOS_END,                       /* 2108-01-01 00:00:00 */
            UINT64_C(0x7FFFFFFFFFFFFFFF),     /* the largest value below 2^63 */
            UINT64_C(0x8000000000000000),
            UINT64_C(0xFFFFFFFFFFFFFFFF),
        };
        int i;

        for (i = 0; i < (int)(sizeof outside / sizeof outside[0]); i++) {
            ft = filetime(outside[i]);
            d = 0x5A5A; t = 0x5A5A;
            CALL(kernel32, SetLastError, 0);
            T_EQ(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, &t), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
            T_EQ(d, 0x5A5A);
            T_EQ(t, 0x5A5A);
        }
    }

    T_CASE("success does not change last error");
    ft = filetime(rt_time_filetime_from_unix(946684800, 0));
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, &t), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, FileTimeToDosDateTime, NULL, &d, &t), "NULL lpFileTime");
    T_TRAPS(CALL(kernel32, FileTimeToDosDateTime, &ft, NULL, &t), "NULL lpFatDate");
    T_TRAPS(CALL(kernel32, FileTimeToDosDateTime, &ft, &d, NULL), "NULL lpFatTime");
}
