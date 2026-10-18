/* rt_filetime_from_systemtime (core/locale.c; the specification is rt/locale.h).
 *
 * Where the expected values come from:
 *   - four pairs that Wine's kernel32/tests/time.c (test_conversions) requires of Windows, each
 *     recomputed with Python's datetime as (date - 1601-01-01) in 100 ns units;
 *   - a calendar walked forward one day at a time in this file, from 1601-01-01 (FILETIME 0, a
 *     Monday) to 30827-12-31, by the rule "a leap year is every fourth year but not a century
 *     that 400 does not divide": day number n is FILETIME n * 864,000,000,000.  The walk shares
 *     no code with the runtime; its last day number, 10,674,941, is 73 cycles of 146,097 days
 *     and the days from 1601-01-01 to 1627-12-31 as Python's datetime counts them;
 *   - 2026-10-10, the day this was written, a Saturday.
 * The refusals are the ranges of Microsoft's page of SYSTEMTIME; the error code is the one
 * Wine's test requires for the year 1600.
 */
#include "rt/test.h"
#include "rt/locale.h"

#define DAY   UINT64_C(864000000000)
#define UNSET UINT64_C(0x1122334455667788)

static RtSystemTime st_of(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s, unsigned ms)
{
    RtSystemTime st = { .wYear = (WORD)y, .wMonth = (WORD)mo, .wDayOfWeek = 0, .wDay = (WORD)d,
                        .wHour = (WORD)h, .wMinute = (WORD)mi, .wSecond = (WORD)s, .wMilliseconds = (WORD)ms };
    return st;
}

static uint64_t ft_of(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s, unsigned ms)
{
    RtSystemTime st = st_of(y, mo, d, h, mi, s, ms);
    uint64_t ft = UNSET;
    T_TRUE(rt_filetime_from_systemtime(&st, &ft));
    return ft;
}

static void refused(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned s, unsigned ms)
{
    RtSystemTime st = st_of(y, mo, d, h, mi, s, ms);
    uint64_t ft = UNSET;
    rt_set_last_error(T_CTX(), 0x5EA1);
    T_EQ(rt_filetime_from_systemtime(&st, &ft), false);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(ft, UNSET);
}

TEST(core, time_calendar)
{
    Ctx *ctx = T_CTX();

    T_CASE("the values Wine's tests require of Windows");
    rt_set_last_error(ctx, 0x5EA1);
    T_EQ(ft_of(1601, 1, 1, 0, 0, 0, 0), 0);
    T_EQ(ft_of(1980, 1, 1, 0, 0, 0, 0), UINT64_C(0x01A8E79FE1D58000));
    T_EQ(ft_of(2002, 5, 1, 12, 0, 0, 0), UINT64_C(0x01C1F107B82B6000));
    T_EQ(ft_of(2002, 7, 26, 11, 55, 32, 123), UINT64_C(0x01C2349B580716B0));
    T_EQ(ft_of(1970, 1, 1, 0, 0, 0, 0), UINT64_C(0x019DB1DED53E8000));       /* the Unix epoch */
    T_EQ(ft_of(30827, 12, 31, 23, 59, 59, 999), UINT64_C(0x7FFF35F4F06C58F0)); /* the last valid time */
    T_EQ(T_LAST_ERROR(), 0x5EA1);                                           /* success leaves it alone */

    T_CASE("the day of the week is not read");
    {
        RtSystemTime st = st_of(2026, 10, 10, 9, 30, 0, 0);
        uint64_t a = UNSET, b = UNSET;
        st.wDayOfWeek = 3;                                                  /* it was a Saturday (6) */
        T_TRUE(rt_filetime_from_systemtime(&st, &a));
        st.wDayOfWeek = 0xFFFF;
        T_TRUE(rt_filetime_from_systemtime(&st, &b));
        T_EQ(a, b);
        RtSystemTime back;
        rt_systemtime_from_filetime(a, &back);
        T_EQ(back.wDayOfWeek, 6);
    }

    T_CASE("every day from 1601 to 30827 agrees with a calendar walked day by day, and comes back the same");
    {
        static const unsigned mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        uint64_t n = 0, bad = 0;
        for (unsigned y = 1601; y <= 30827; y++) {
            const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
            for (unsigned mo = 1; mo <= 12; mo++) {
                const unsigned last = mdays[mo - 1] + (mo == 2 && leap);
                for (unsigned d = 1; d <= last; d++, n++) {
                    /* a time of day that changes from day to day and reaches both ends */
                    const unsigned h = (unsigned)(n % 24), mi = (unsigned)(n % 60), s = (unsigned)((n / 7) % 60),
                                   ms = (unsigned)((n * 37) % 1000);
                    RtSystemTime st = st_of(y, mo, d, h, mi, s, ms), back;
                    uint64_t ft = UNSET;
                    const uint64_t want = n * DAY + (((uint64_t)h * 60 + mi) * 60 + s) * 10000000 + (uint64_t)ms * 10000;
                    if (!rt_filetime_from_systemtime(&st, &ft) || ft != want) { bad++; continue; }
                    rt_systemtime_from_filetime(ft, &back);
                    if (back.wYear != y || back.wMonth != mo || back.wDay != d || back.wHour != h ||
                        back.wMinute != mi || back.wSecond != s || back.wMilliseconds != ms ||
                        back.wDayOfWeek != (n + 1) % 7)                     /* day 0 was a Monday */
                        bad++;
                }
            }
        }
        T_EQ(n, 10674942);                                                  /* days walked: the last has number 10,674,941 */
        T_EQ(bad, 0);
    }

    T_CASE("29 February exists in a leap year only");
    T_EQ(ft_of(2000, 2, 29, 0, 0, 0, 0), UINT64_C(0x01BF8247EBCC8000));
    T_EQ(ft_of(2024, 3, 1, 0, 0, 0, 0) - ft_of(2024, 2, 28, 0, 0, 0, 0), 2 * DAY);
    T_EQ(ft_of(2023, 3, 1, 0, 0, 0, 0) - ft_of(2023, 2, 28, 0, 0, 0, 0), 1 * DAY);
    T_EQ(ft_of(1900, 3, 1, 0, 0, 0, 0) - ft_of(1900, 2, 28, 0, 0, 0, 0), 1 * DAY);
    refused(2023, 2, 29, 0, 0, 0, 0);
    refused(1900, 2, 29, 0, 0, 0, 0);
    refused(2100, 2, 29, 0, 0, 0, 0);

    T_CASE("a member out of range is refused with ERROR_INVALID_PARAMETER and nothing is written");
    refused(1600, 12, 31, 23, 59, 59, 999);                                 /* Wine test_conversions */
    refused(1980, 0, 1, 0, 0, 0, 0);                                        /* Wine test_invalid_arg: month */
    refused(1980, 1, 1, 24, 0, 0, 0);                                       /* hour */
    refused(1980, 1, 1, 0, 60, 0, 0);                                       /* minute */
    refused(0, 1, 1, 0, 0, 0, 0);
    refused(30828, 1, 1, 0, 0, 0, 0);
    refused(65535, 1, 1, 0, 0, 0, 0);
    refused(1980, 13, 1, 0, 0, 0, 0);
    refused(1980, 1, 0, 0, 0, 0, 0);
    refused(1980, 1, 32, 0, 0, 0, 0);
    refused(1980, 4, 31, 0, 0, 0, 0);
    refused(1980, 2, 30, 0, 0, 0, 0);
    refused(1980, 1, 1, 0, 0, 60, 0);
    refused(1980, 1, 1, 0, 0, 0, 1000);
    refused(1980, 1, 1, 65535, 0, 0, 0);

    T_CASE("a NULL pointer is fatal");
    {
        RtSystemTime st = st_of(1980, 1, 1, 0, 0, 0, 0);
        uint64_t ft;
        T_TRAPS(rt_filetime_from_systemtime(NULL, &ft), "rt_filetime_from_systemtime: a NULL pointer");
        T_TRAPS(rt_filetime_from_systemtime(&st, NULL), "rt_filetime_from_systemtime: a NULL pointer");
    }
}
