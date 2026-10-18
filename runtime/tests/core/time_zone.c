/* The time zone as TIME_ZONE_INFORMATION.Bias (docs/RUNTIME.md section 8.1): the host's
 * current offset, UTC = local time + bias, in minutes, positive west of Greenwich, with no
 * daylight-saving rule.
 *
 * The zone is set with TZ, which the host's C library reads.  Each test sets TZ in its own
 * process (the runner forks one per test), so tests do not see each other's zone.
 */
#include <stdlib.h>
#include <time.h>

#include "rt/test.h"
#include "rt/time.h"

static void set_tz(const char *tz) {
    setenv("TZ", tz, 1);
    tzset();
}

/* The host's bias from its own calendar: -(seconds east of UTC) / 60.  Also reports whether
 * daylight time is in effect. */
static long host_bias(int *dst) {
    tzset();
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    if (dst) *dst = local.tm_isdst;
    return -(local.tm_gmtoff / 60);
}

TEST(core, zone_matches_the_host)
{
    int dst = 0;
    long want = host_bias(&dst);
    T_EQ(rt_time_zone_bias_minutes(), want);
    /* Real zones run from UTC-12 to UTC+14, so their bias is from -840 to +720 minutes. */
    T_TRUE(want >= -840 && want <= 720);
}

TEST(core, zone_fixed_offsets_set_at_run_time)
{
    /* Each zone is installed while the runtime runs, so this also shows that a change of
     * zone is seen by the next call.  The POSIX form "XYZ+8" means UTC-8, which is bias +480. */
    static const struct { const char *tz; LONG bias; } cases[] = {
        { "UTC", 0 },
        { "XYZ+0", 0 },
        { "XYZ+8", 480 },           /* UTC-8, west of Greenwich: positive */
        { "XYZ-5:30", -330 },       /* UTC+5:30 */
        { "XYZ-5:45", -345 },       /* UTC+5:45 */
        { "XYZ+12", 720 },          /* UTC-12, the largest positive bias */
        { "XYZ-14", -840 },         /* UTC+14, the largest negative bias */
        { "XYZ-5:30", -330 },       /* and back, after a different zone */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        set_tz(cases[i].tz);
        T_EQ(rt_time_zone_bias_minutes(), cases[i].bias);
    }
}

TEST(core, zone_reports_the_offset_in_effect_without_a_rule)
{
    /* A zone with a daylight rule (US Eastern): the bias is the offset in effect now, which is
     * UTC-5 (bias 300) in winter and UTC-4 (bias 240) in summer.  No DaylightBias is reported. */
    set_tz("EST5EDT,M3.2.0,M11.1.0");
    int dst = 0;
    long want = host_bias(&dst);
    T_EQ(want, dst ? 240 : 300);
    T_EQ(rt_time_zone_bias_minutes(), want);
}

TEST(core, zone_refuses_an_offset_that_is_not_whole_minutes)
{
    /* UTC+5:30:15.  TIME_ZONE_INFORMATION holds minutes only, so the answer would be wrong
     * by fifteen seconds; the runtime stops and names the offset instead. */
    set_tz("XYZ-5:30:15");
    T_TRAPS((void)rt_time_zone_bias_minutes(), "the host's UTC offset is 19815 seconds, not a whole number of minutes");
    set_tz("UTC");
}
