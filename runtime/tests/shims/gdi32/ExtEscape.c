#include "rt/test.h"

/* True if the n bytes at p are all v. */
static int ext_bytes_are(const unsigned char *p, int n, unsigned char v)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != v) return 0;
    return 1;
}

TEST(gdi32, ExtEscape)
{
    unsigned char in[16], out[16];
    HDC dc = (HDC)(uintptr_t)0x1234;
    int n;

    /* The first call of the test is escape 8, so its log line is the one the case below checks. */
    T_CASE("escape 8 (QUERYESCSUPPORT) returns 0 and leaves the buffers alone");
    memset(in, 0xA5, sizeof in);
    memset(out, 0xA5, sizeof out);
    T_EQ(CALL(gdi32, ExtEscape, dc, 8, 16, in, 16, out), 0);
    T_TRUE(ext_bytes_are(in, 16, 0xA5));
    T_TRUE(ext_bytes_are(out, 16, 0xA5));

    T_CASE("application-defined escape 0x110000 returns 0 and leaves the buffers alone");
    memset(in, 0xA5, sizeof in);
    memset(out, 0xA5, sizeof out);
    T_EQ(CALL(gdi32, ExtEscape, dc, 0x110000, 16, in, 16, out), 0);
    T_TRUE(ext_bytes_are(in, 16, 0xA5));
    T_TRUE(ext_bytes_are(out, 16, 0xA5));

    T_CASE("NULL and non-NULL device context");
    memset(in, 0xA5, sizeof in);
    memset(out, 0xA5, sizeof out);
    T_EQ(CALL(gdi32, ExtEscape, NULL, 8, 16, in, 16, out), 0);
    T_EQ(CALL(gdi32, ExtEscape, dc, 0x110000, 16, in, 16, out), 0);
    T_TRUE(ext_bytes_are(in, 16, 0xA5));
    T_TRUE(ext_bytes_are(out, 16, 0xA5));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, ExtEscape, dc, 8, 16, in, 16, out), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Once per process: the escape of the first call names the line; later calls log nothing. */
    T_CASE("logs its line once");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ExtEscape(8): the display has no such feature"));
    n = T_LOG_COUNT(RT_CH_WINDOW);
    T_EQ(CALL(gdi32, ExtEscape, dc, 0x110000, 16, in, 16, out), 0);
    T_EQ(T_LOG_COUNT(RT_CH_WINDOW), n);
}
