/* The FILETIME epoch conversion behind rt_time_utc_100ns (docs/RUNTIME.md section 8.1):
 * 100 ns intervals since 1601-01-01 00:00 UTC.
 *
 * rt_time_filetime_from_unix is not in rt/time.h.  It is declared here, and it is the only
 * thing this file depends on in time.c.  Its results are checked against the host's own
 * calendar (timegm), which knows nothing about the constants in time.c, and against FILETIME
 * values given in the Windows documentation and common references.
 */
#include <string.h>
#include <time.h>

#include "rt/test.h"
#include "rt/time.h"

#define UNITS_PER_SEC  UINT64_C(10000000)
#define SECS_1601      INT64_C(11644473600)     /* 1601-01-01 to 1970-01-01, in seconds */

/* Days from 1970-01-01 to a proleptic Gregorian date (Howard Hinnant's days_from_civil).
 * This is the oracle for the dates, not the host's timegm: on this host timegm returns -1
 * for every date before 1900, with errno unchanged, so it cannot be used there. */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2 ? 1 : 0;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* A date and time in UTC, as seconds since 1970. */
static int64_t unix_at(int year, int mon, int mday, int hour, int min, int sec) {
    return days_from_civil(year, mon, mday) * 86400 + hour * 3600 + min * 60 + sec;
}

/* The same from the host's timegm, for the dates it handles (1900 onwards). */
static int64_t host_unix_at(int year, int mon, int mday, int hour, int min, int sec) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = mday;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    return (int64_t)timegm(&tm);
}

TEST(core, epoch_day_count_agrees_with_host_and_constant)
{
    /* The offset between the two epochs.  369 years from 1601 to 1970 with 89 leap days is
     * 134,774 days, which is the constant 11644473600 seconds. */
    T_EQ(unix_at(1601, 1, 1, 0, 0, 0), -SECS_1601);
    T_EQ(unix_at(1970, 1, 1, 0, 0, 0), 0);
    T_EQ(unix_at(1601, 1, 2, 0, 0, 0), -SECS_1601 + 86400);
    T_EQ(unix_at(1970, 1, 1, 0, 0, 0) - unix_at(1601, 1, 1, 0, 0, 0), INT64_C(134774) * 86400);
    T_EQ(days_from_civil(1601, 1, 1), -134774);

    /* The oracle checked against the host's calendar where the host's calendar works. */
    static const int dates[][3] = { {1900, 1, 1}, {1900, 3, 1}, {1999, 12, 31}, {2000, 2, 29},
                                    {2024, 2, 29}, {2038, 1, 19}, {2100, 3, 1}, {2400, 2, 29} };
    for (unsigned i = 0; i < sizeof dates / sizeof dates[0]; i++)
        T_EQ(unix_at(dates[i][0], dates[i][1], dates[i][2], 0, 0, 0),
             host_unix_at(dates[i][0], dates[i][1], dates[i][2], 0, 0, 0));
    T_EQ(unix_at(1900, 1, 1, 0, 0, 0), INT64_C(-2208988800));
}

TEST(core, epoch_known_dates)
{
    /* FILETIME values for dates the Windows documentation and common references use. */
    T_EQ(rt_time_filetime_from_unix(-SECS_1601, 0), UINT64_C(0));                              /* 1601-01-01 */
    T_EQ(rt_time_filetime_from_unix(0, 0), UINT64_C(116444736000000000));                      /* 1970-01-01 */
    T_EQ(rt_time_filetime_from_unix(946684800, 0), UINT64_C(125911584000000000));              /* 2000-01-01 */
    T_EQ(unix_at(2000, 2, 29, 0, 0, 0), 951782400);                                            /* leap day */
    T_EQ(rt_time_filetime_from_unix(951782400, 0), UINT64_C(125962560000000000));              /* 2000-02-29 */
    T_EQ(unix_at(2024, 2, 29, 0, 0, 0), 1709164800);
    T_EQ(rt_time_filetime_from_unix(1709164800, 0), UINT64_C(133536384000000000));             /* 2024-02-29 */
    T_EQ(rt_time_filetime_from_unix(-SECS_1601 + 86400, 0), UINT64_C(864000000000));           /* 1601-01-02 */
}

TEST(core, epoch_other_dates_by_host_calendar)
{
    /* Dates on both sides of 1970, the end of a 32-bit time_t, and today.  Each is compared
     * with the FILETIME the host calendar gives: seconds since 1601 times 10^7, plus the
     * nanoseconds in 100 ns units. */
    static const struct { int y, mo, d, h, mi, s; int64_t ns; } dates[] = {
        { 1900, 1, 1, 0, 0, 0, 0 },
        { 1999, 12, 31, 23, 59, 59, 999999999 },
        { 2000, 2, 29, 12, 30, 15, 123456789 },
        { 2038, 1, 19, 3, 14, 7, 0 },
        { 2100, 3, 1, 0, 0, 0, 0 },
        { 2026, 10, 8, 0, 0, 0, 0 },
        { 2026, 10, 8, 13, 45, 30, 500000000 },
    };
    for (unsigned i = 0; i < sizeof dates / sizeof dates[0]; i++) {
        int64_t sec = unix_at(dates[i].y, dates[i].mo, dates[i].d, dates[i].h, dates[i].mi, dates[i].s);
        uint64_t want = (uint64_t)(sec + SECS_1601) * UNITS_PER_SEC + (uint64_t)dates[i].ns / 100;
        T_EQ(rt_time_filetime_from_unix(sec, dates[i].ns), want);
    }
}

TEST(core, epoch_truncates_nanoseconds)
{
    /* Nanoseconds are cut to 100 ns units, not rounded, and a value just under a second does
     * not carry into the next second. */
    T_EQ(rt_time_filetime_from_unix(-SECS_1601, 0), UINT64_C(0));
    T_EQ(rt_time_filetime_from_unix(-SECS_1601, 99), UINT64_C(0));
    T_EQ(rt_time_filetime_from_unix(-SECS_1601, 100), UINT64_C(1));
    T_EQ(rt_time_filetime_from_unix(-SECS_1601, 199), UINT64_C(1));
    T_EQ(rt_time_filetime_from_unix(-SECS_1601, 999999999), UINT64_C(9999999));
    T_EQ(rt_time_filetime_from_unix(-SECS_1601 + 1, 0), UINT64_C(10000000));
}

TEST(core, epoch_largest_representable)
{
    /* FILETIME is 64 bits of 100 ns units, so the largest value is UINT64_MAX.  The last second
     * that fits starts at s_max = UINT64_MAX / 10^7 seconds after 1601: its first 100 ns unit
     * still fits, but 999,999,999 ns (9,999,999 units) pushes past UINT64_MAX. */
    uint64_t s_max = UINT64_MAX / UNITS_PER_SEC;                /* 1844674407370 */
    int64_t sec_max = (int64_t)s_max - SECS_1601;
    T_EQ(rt_time_filetime_from_unix(sec_max, 0), s_max * UNITS_PER_SEC);
    T_EQ(rt_time_filetime_from_unix(sec_max, 100), s_max * UNITS_PER_SEC + 1);
    T_EQ(rt_time_filetime_from_unix(sec_max - 1, 999999999), (s_max - 1) * UNITS_PER_SEC + 9999999);
    T_TRAPS((void)rt_time_filetime_from_unix(sec_max, 999999999), "beyond what FILETIME can hold");
    T_TRAPS((void)rt_time_filetime_from_unix(sec_max + 1, 0), "beyond what FILETIME can hold");
    T_TRAPS((void)rt_time_filetime_from_unix(INT64_MAX, 0), "beyond what FILETIME can hold");
}

TEST(core, epoch_refuses_before_1601)
{
    T_TRAPS((void)rt_time_filetime_from_unix(-SECS_1601 - 1, 0), "before 1601-01-01");
    T_TRAPS((void)rt_time_filetime_from_unix(-SECS_1601 - 1, 999999999), "before 1601-01-01");
    T_TRAPS((void)rt_time_filetime_from_unix(INT64_MIN, 0), "before 1601-01-01");
}

TEST(core, epoch_refuses_bad_nanoseconds)
{
    T_TRAPS((void)rt_time_filetime_from_unix(0, -1), "-1 nanoseconds is out of range");
    T_TRAPS((void)rt_time_filetime_from_unix(0, 1000000000), "1000000000 nanoseconds is out of range");
    T_TRAPS((void)rt_time_filetime_from_unix(0, INT64_MAX), "is out of range");
}
