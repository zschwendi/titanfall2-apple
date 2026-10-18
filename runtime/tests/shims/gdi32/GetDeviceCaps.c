#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* The oracle is the core's desktop numbers (rt/window.h, rt_desktop_device_cap), which are the
 * values the brief fixes for every context: HORZRES 1920, VERTRES 1080, BITSPIXEL 32, PLANES 1,
 * LOGPIXELSX and LOGPIXELSY 96, VREFRESH 60.  Wine gdi32/tests/dc.c, test_dc_values, for the
 * handle that is no context (ERROR_INVALID_HANDLE). */

/* The desktop's numbers through one context: every answer the brief names, and last error
 * left as it was. */
static void check_desktop(HDC dc)
{
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, HORZRES), 1920);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, VERTRES), 1080);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, BITSPIXEL), 32);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, PLANES), 1);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, LOGPIXELSX), 96);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, LOGPIXELSY), 96);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, VREFRESH), 60);
    T_EQ(T_LAST_ERROR(), 0x1234);
}

TEST(gdi32, GetDeviceCaps)
{
    HDC s = rt_dc_get(NULL);
    HDC m;

    T_CASE("the desktop's sizes, depth, pixels per inch and refresh rate for a display context");
    T_REQUIRE(s != NULL);
    check_desktop(s);

    T_CASE("the same for a memory context");
    m = rt_dc_create_compatible(NULL);
    T_REQUIRE(m != NULL);
    check_desktop(m);

    T_CASE("a handle that is no context");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(gdi32, GetDeviceCaps, (HDC)(uintptr_t)0xdeadbeef, HORZRES), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(gdi32, GetDeviceCaps, s, VERTRES), 1080);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The index 38 (RASTERCAPS) is not answered by the desktop: a stop of the core. */
    T_TRAPS(CALL(gdi32, GetDeviceCaps, s, 38), "rt_dc_device_cap: not implemented");
}
