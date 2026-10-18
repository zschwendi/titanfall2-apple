#include "rt/test.h"
#include "rt/win/user.h"

/* True if the n bytes at p are all v. */
static int bytes_are(const unsigned char *p, int n, unsigned char v)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != v) return 0;
    return 1;
}

TEST(user32, TranslateAcceleratorA)
{
    HWND wnd = (HWND)(uintptr_t)0x1234;
    HACCEL tab = (HACCEL)(uintptr_t)0x1234;
    MSG msg;
    int n;

    T_CASE("NULL table returns 0 with ERROR_INVALID_ACCEL_HANDLE");
    T_EQ(CALL(user32, TranslateAcceleratorA, wnd, NULL, NULL), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ACCEL_HANDLE);

    T_CASE("(HACCEL)0x1234 returns 0 with ERROR_INVALID_ACCEL_HANDLE");
    T_EQ(CALL(user32, TranslateAcceleratorA, wnd, tab, NULL), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ACCEL_HANDLE);

    T_CASE("NULL and non-NULL MSG with preset bytes unchanged");
    memset(&msg, 0xA5, sizeof msg);
    T_EQ(CALL(user32, TranslateAcceleratorA, wnd, NULL, &msg), 0);
    T_TRUE(bytes_are((const unsigned char *)&msg, sizeof msg, 0xA5));
    T_EQ(CALL(user32, TranslateAcceleratorA, wnd, tab, &msg), 0);
    T_TRUE(bytes_are((const unsigned char *)&msg, sizeof msg, 0xA5));
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ACCEL_HANDLE);

    /* Once per process: the first call logs the line; later calls log nothing. */
    T_CASE("logs its line once at debug level");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "TranslateAcceleratorA: no accelerator table exists"));
    n = T_LOG_COUNT(RT_CH_WINDOW);
    T_EQ(CALL(user32, TranslateAcceleratorA, wnd, tab, NULL), 0);
    T_EQ(T_LOG_COUNT(RT_CH_WINDOW), n);
}
