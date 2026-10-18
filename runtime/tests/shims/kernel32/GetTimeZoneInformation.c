#include <string.h>

#include "rt/test.h"
#include "rt/time.h"
#include "rt/win/sysinfo.h"

/* The guest's buffer: the structure, then 8 bytes past its end that the shim must not write.
 * The whole buffer starts as 0xA5, so a byte the shim should clear that it left alone shows as
 * 0xA5, and a byte past the structure that it wrote shows as anything but 0xA5. */
struct zone_buf {
    TIME_ZONE_INFORMATION tz;
    unsigned char after[8];
};
_Static_assert(sizeof(struct zone_buf) == 180, "zone_buf is the structure and 8 bytes after it");

/* The number of bytes in p[0..n) that are not v. */
static size_t not_equal(const unsigned char *p, size_t n, unsigned char v)
{
    size_t bad = 0;
    for (size_t i = 0; i < n; i++)
        if (p[i] != v) bad++;
    return bad;
}

TEST(kernel32, GetTimeZoneInformation)
{
    struct zone_buf b;
    const unsigned char *raw = (const unsigned char *)&b.tz;
    LONG want;

    T_CASE("fills Bias with the host's offset from UTC in minutes");
    memset(&b, 0xA5, sizeof b);
    want = rt_time_zone_bias_minutes();
    CALL(kernel32, GetTimeZoneInformation, &b.tz);
    T_EQ(b.tz.Bias, want);

    T_CASE("reports TIME_ZONE_ID_UNKNOWN with zero biases, zero transition dates and empty names");
    memset(&b, 0xA5, sizeof b);
    T_EQ(CALL(kernel32, GetTimeZoneInformation, &b.tz), TIME_ZONE_ID_UNKNOWN);
    T_EQ(b.tz.StandardBias, 0);
    T_EQ(b.tz.DaylightBias, 0);
    T_EQ(b.tz.StandardDate.wMonth, 0);
    T_EQ(b.tz.DaylightDate.wMonth, 0);
    T_EQ(b.tz.StandardName[0], 0);
    T_EQ(b.tz.DaylightName[0], 0);
    /* Every byte of the structure other than Bias (bytes 0 to 3) is 0: 168 bytes. */
    T_EQ(not_equal(raw + 4, sizeof b.tz - 4, 0), 0);
    /* The 8 bytes after the structure are not written. */
    T_EQ(not_equal(b.after, sizeof b.after, 0xA5), 0);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetTimeZoneInformation, &b.tz);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* A NULL pointer is refused (the documentation does not say what it does). */
    T_TRAPS(CALL(kernel32, GetTimeZoneInformation, NULL), "GetTimeZoneInformation with NULL lpTimeZoneInformation");
}
