#include "rt/test.h"

/* True if the n bytes at p are all v. */
static int ramp_bytes_are(const unsigned char *p, int n, unsigned char v)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != v) return 0;
    return 1;
}

TEST(gdi32, GetDeviceGammaRamp)
{
    /* Three arrays of 256 WORD (red, green, blue): 3 * 256 * 2 = 1536 bytes. */
    unsigned char ramp[1536];
    HDC dc = (HDC)(uintptr_t)0x1234;
    int n;

    /* The first call of the test is the one whose log line the logs case checks. */
    T_CASE("FALSE with a 1536-byte ramp buffer untouched");
    memset(ramp, 0xA5, sizeof ramp);
    T_EQ(CALL(gdi32, GetDeviceGammaRamp, dc, ramp), FALSE);
    T_TRUE(ramp_bytes_are(ramp, 1536, 0xA5));

    T_CASE("NULL and non-NULL device context");
    memset(ramp, 0xA5, sizeof ramp);
    T_EQ(CALL(gdi32, GetDeviceGammaRamp, NULL, ramp), FALSE);
    T_EQ(CALL(gdi32, GetDeviceGammaRamp, dc, ramp), FALSE);
    T_TRUE(ramp_bytes_are(ramp, 1536, 0xA5));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, GetDeviceGammaRamp, dc, ramp), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Once per process: the first call logs the line; later calls log nothing. */
    T_CASE("logs its line once");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "GetDeviceGammaRamp: the display has no such feature"));
    n = T_LOG_COUNT(RT_CH_WINDOW);
    T_EQ(CALL(gdi32, GetDeviceGammaRamp, dc, ramp), FALSE);
    T_EQ(T_LOG_COUNT(RT_CH_WINDOW), n);
}
