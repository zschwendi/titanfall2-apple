#include "rt/test.h"
#include "rt/shim.h"
#include "rt/gdi.h"

/* A display context is one whose device caps answer for the screen: BITSPIXEL is 32 (the
 * core's desktop, rt_desktop_device_cap).  The names are Wine gdi32/tests/dc.c,
 * test_CreateDC (review D13): driver and device compared without regard to ASCII case. */
static void display_dc(HDC dc)
{
    T_TRUE(dc != NULL);
    T_EQ(rt_dc_device_cap(dc, BITSPIXEL), 32);
    T_EQ(rt_dc_delete(dc), TRUE);
}

TEST(gdi32, CreateDCW)
{
    HDC dc;

    CALL(kernel32, SetLastError, 0x1234);

    T_CASE("display context for DISPLAY with no device");
    dc = CALL(gdi32, CreateDCW, u"DISPLAY", NULL, NULL, NULL);
    display_dc(dc);

    T_CASE("driver names compared without regard to ASCII case");
    dc = CALL(gdi32, CreateDCW, u"display", NULL, NULL, NULL);
    display_dc(dc);

    T_CASE("DISPLAY with any device");
    dc = CALL(gdi32, CreateDCW, u"DISPLAY", u"xyz", NULL, NULL);
    display_dc(dc);

    T_CASE("the adapter's name as driver, or as device with no driver");
    dc = CALL(gdi32, CreateDCW, u"\\\\.\\DISPLAY1", NULL, NULL, NULL);
    display_dc(dc);
    dc = CALL(gdi32, CreateDCW, NULL, u"\\\\.\\DISPLAY1", NULL, NULL);
    display_dc(dc);

    T_CASE("NULL for no driver and no device, for a NULL driver with device DISPLAY, and for a printer");
    T_EQ(CALL(gdi32, CreateDCW, NULL, NULL, NULL, NULL), NULL);
    T_EQ(CALL(gdi32, CreateDCW, NULL, u"display", NULL, NULL), NULL);
    T_EQ(CALL(gdi32, CreateDCW, u"WINSPOOL", u"Printer", NULL, NULL), NULL);

    T_CASE("last error unchanged");
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The two stops the core makes itself, and the two arguments this shim refuses. */
    {
        static const DWORD init = 0;
        HDC s = CALL(gdi32, CreateDCW, u"DISPLAY", NULL, NULL, NULL);
        HDC g = rt_dc_get(NULL);
        T_REQUIRE(s != NULL && g != NULL);
        T_TRAPS(CALL(gdi32, CreateDCW, u"DISPLAY", NULL, u"LPT1", NULL), "CreateDCW with pszPort");
        T_TRAPS(CALL(gdi32, CreateDCW, u"DISPLAY", NULL, NULL, &init), "CreateDCW with pdm");
        T_TRAPS(rt_dc_device_cap(s, 38), "rt_dc_device_cap: not implemented");
        T_TRAPS(rt_dc_delete(g), "rt_dc_delete: not implemented");
    }
}
