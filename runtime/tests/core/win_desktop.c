/* The desktop's description, the two lookup tables, the frame of a frameless window and the
 * activation getters (rt/window.h, "the desktop"; core/win.c).  Expected values: the trace
 * of the original (docs/findings/30-proton-trace.md section 9: 1920x1080@60, B8G8R8A8) and
 * the header's own table; none comes from lifted code. */
#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/gdi.h"

TEST(core, win_desktop)
{
    T_CASE("the desktop is one 1920x1080 monitor at the origin, 32 bits, 60 Hz, 96 dpi");
    const RtDesktop *d = rt_desktop();
    T_REQUIRE(d != NULL);
    T_EQ(d->width, 1920); T_EQ(d->height, 1080);
    T_EQ(d->bits_per_pixel, 32); T_EQ(d->refresh_hz, 60); T_EQ(d->dpi, 96);
    T_EQ(d->monitor.left, 0); T_EQ(d->monitor.top, 0); T_EQ(d->monitor.right, 1920); T_EQ(d->monitor.bottom, 1080);
    T_TRUE(memcmp(&d->monitor, &d->work, sizeof(RECT)) == 0);
    T_TRUE(rt_desktop() == d);

    T_CASE("metrics that are answered");
    static const struct { INT index, value; } m[] = {
        { SM_CXSCREEN, 1920 }, { SM_CYSCREEN, 1080 }, { SM_CXVIRTUALSCREEN, 1920 }, { SM_CYVIRTUALSCREEN, 1080 },
        { SM_XVIRTUALSCREEN, 0 }, { SM_YVIRTUALSCREEN, 0 }, { SM_CMONITORS, 1 }, { SM_SAMEDISPLAYFORMAT, 1 },
        { SM_REMOTESESSION, 0 }, { SM_CLEANBOOT, 0 }, { SM_SWAPBUTTON, 0 }, { SM_CXICON, 32 }, { SM_CYICON, 32 },
        { SM_CXCURSOR, 32 }, { SM_CYCURSOR, 32 }, { SM_CXSMICON, 16 }, { SM_CYSMICON, 16 },
    };
    rt_thread_set_last_error(0xDEAD);
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) {
        INT v = -77;
        T_TRUE(rt_desktop_metric(m[i].index, &v));
        T_EQ(v, m[i].value);
    }

    T_CASE("a metric that is not in the table is refused and nothing is written");
    static const INT no[] = { 4 /* SM_CYCAPTION */, 5, 6, 7, 8, 32, 33, 19 /* SM_MOUSEPRESENT */, 43, 75, 92, -1, 10000 };
    for (unsigned i = 0; i < sizeof no / sizeof no[0]; i++) {
        INT v = -77;
        T_TRUE(!rt_desktop_metric(no[i], &v));
        T_EQ(v, -77);
    }

    T_CASE("device capabilities of the display");
    static const struct { INT index, value; } c[] = {
        { HORZRES, 1920 }, { VERTRES, 1080 }, { DESKTOPHORZRES, 1920 }, { DESKTOPVERTRES, 1080 }, { BITSPIXEL, 32 },
        { PLANES, 1 }, { LOGPIXELSX, 96 }, { LOGPIXELSY, 96 }, { VREFRESH, 60 }, { HORZSIZE, 508 }, { VERTSIZE, 286 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        INT v = -77;
        T_TRUE(rt_desktop_device_cap(c[i].index, &v));
        T_EQ(v, c[i].value);
    }
    INT v = -77;
    T_TRUE(!rt_desktop_device_cap(24 /* NUMCOLORS */, &v));
    T_TRUE(!rt_desktop_device_cap(38 /* RASTERCAPS */, &v));
    T_EQ(v, -77);

    T_CASE("a style with no frame has no insets; a style with one is refused");
    RECT r = { 9, 9, 9, 9 };
    T_TRUE(rt_window_frame(WS_POPUP, 0, FALSE, &r));
    T_TRUE(r.left == 0 && r.top == 0 && r.right == 0 && r.bottom == 0);
    r = (RECT){ 9, 9, 9, 9 };
    T_TRUE(rt_window_frame(WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU, WS_EX_TOPMOST, FALSE, &r));
    T_EQ(r.bottom, 0);
    r = (RECT){ 9, 9, 9, 9 };
    T_TRUE(rt_window_frame(WS_POPUP, WS_EX_WINDOWEDGE, FALSE, &r));     /* Wine test_AWR_flags: zero insets */
    T_TRUE(r.left == 0 && r.top == 0 && r.right == 0 && r.bottom == 0);

    /* Expected insets: review N4's algorithm (Wine user32/tests/win.c, wine_AdjustWindowRectEx)
     * worked by hand, with this desktop's caption height of 22 (rt/window.h, rt_window_frame). */
    T_CASE("fixed frames: the checked algorithm with this desktop's caption height");
    static const struct { DWORD style, ex; LONG side, top; } fixed[] = {
        { WS_BORDER, 0, 1, 1 }, { WS_DLGFRAME, 0, 3, 3 }, { WS_CAPTION, 0, 3, 26 },
        { WS_POPUP, WS_EX_DLGMODALFRAME, 3, 3 }, { WS_POPUP, WS_EX_CLIENTEDGE, 2, 2 }, { WS_POPUP, WS_EX_STATICEDGE, 1, 1 },
        { WS_POPUP | WS_BORDER, WS_EX_STATICEDGE, 2, 2 },
        { 0x84ca0000u, 0x00040300u, 5, 28 },                            /* the game's window (docs/trace/window-calls.md) */
    };
    for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
        r = (RECT){ 9, 9, 9, 9 };
        T_TRUE(rt_window_frame(fixed[i].style, fixed[i].ex, FALSE, &r));
        T_EQ(r.left, fixed[i].side); T_EQ(r.right, fixed[i].side); T_EQ(r.bottom, fixed[i].side); T_EQ(r.top, fixed[i].top);
    }
    T_EQ(rt_desktop()->caption_height, 22);

    T_CASE("the three frames whose number this desktop does not have are refused");
    r = (RECT){ 9, 9, 9, 9 };
    T_TRUE(!rt_window_frame(WS_THICKFRAME, 0, FALSE, &r));
    T_TRUE(!rt_window_frame(WS_POPUP | WS_THICKFRAME, 0, FALSE, &r));
    T_TRUE(!rt_window_frame(WS_CAPTION, WS_EX_TOOLWINDOW, FALSE, &r));
    T_TRUE(!rt_window_frame(WS_POPUP, 0, TRUE, &r));
    T_TRUE(r.left == 9 && r.top == 9 && r.right == 9 && r.bottom == 9);

    T_CASE("no window is active, focused or in the foreground");
    T_TRUE(rt_window_active() == NULL);
    T_TRUE(rt_window_focus() == NULL);
    T_TRUE(rt_window_foreground() == NULL);
    T_EQ(T_LAST_ERROR(), 0xDEAD);                   /* nothing above touched last error */
}
