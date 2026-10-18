/* Expected values.  The cases without a device context: the row `enum-monitors`
 * (tools/swarm/shim_rows_window.py) and Microsoft's page.  The cases with one (unit
 * gate3-run-3): Microsoft's pages of EnumDisplayMonitors and MonitorEnumProc, read 2026-10-10
 * ("enumerates all display monitors that intersect the visible region of the device context";
 * the callback's rectangle "is the intersection of the clipping area of the device context
 * identified by hdcMonitor and the display monitor rectangle", in "device-context
 * coordinates"; returning FALSE stops the enumeration), with the numbers worked out by hand
 * from the windows made below and the 1920 x 1080 monitor of rt_desktop.  That the callback
 * is handed the caller's own context is this runtime's choice (the shim's comment). */
#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/gdi.h"
#include "rt/win/user.h"

/* The callback: records what the shim passes it and returns g_ret.  The RECT is read through r8. */
static int      g_calls;
static uint64_t g_rcx, g_rdx, g_data, g_ret;
static LONG     g_rect[4];

GUEST_FN(enum_proc)
{
    const LONG *rc = (const LONG *)(uintptr_t)ctx->r[8];
    g_calls++;
    g_rcx = ctx->r[1];
    g_rdx = ctx->r[2];
    g_rect[0] = rc[0];
    g_rect[1] = rc[1];
    g_rect[2] = rc[2];
    g_rect[3] = rc[3];
    g_data = ctx->r[9];
    ctx->r[0] = g_ret;
    GUEST_RET(ctx);
}

/* A window procedure that passes everything on, and a window of its class. */
GUEST_FN(pass_on)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    tf_fp_enter_host();
    const LRESULT r = rt_window_def_proc(ctx, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}
static HWND mk(DWORD style, HWND parent, INT x, INT y, INT cx, INT cy)
{
    static bool reg;
    if (!reg) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(pass_on), .lpszClassName = u"EnumMonWin" };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .style = style, .cls = RT_TEXT_W(u"EnumMonWin"), .title = RT_TEXT_W(u"t"), .x = x, .y = y, .cx = cx, .cy = cy,
                         .parent = parent, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, EnumDisplayMonitors)
{
    RECT clip;

    T_CASE("no clip gives one call with the handle 0x10001, a NULL HDC, the monitor rectangle and the data");
    g_calls = 0;
    g_ret = 1;
    T_EQ(CALL(user32, EnumDisplayMonitors, NULL, NULL, T_GUEST_ADDR(enum_proc), 0x1122334455667788), TRUE);
    T_EQ(g_calls, 1);
    T_EQ(g_rcx, 0x10001);
    T_EQ(g_rdx, 0);
    T_EQ(g_rect[0], 0);
    T_EQ(g_rect[1], 0);
    T_EQ(g_rect[2], 1920);
    T_EQ(g_rect[3], 1080);
    T_EQ(g_data, 0x1122334455667788);

    T_CASE("a clip that intersects the monitor still gives one call");
    g_calls = 0;
    g_ret = 1;
    clip = (RECT){ 100, 100, 200, 200 };
    T_EQ(CALL(user32, EnumDisplayMonitors, NULL, &clip, T_GUEST_ADDR(enum_proc), 7), TRUE);
    T_EQ(g_calls, 1);
    T_EQ(g_rcx, 0x10001);
    T_EQ(g_rect[2], 1920);
    T_EQ(g_rect[3], 1080);

    T_CASE("a clip that misses the monitor gives no call and TRUE");
    g_calls = 0;
    g_ret = 1;
    clip = (RECT){ 5000, 0, 6000, 10 };
    T_EQ(CALL(user32, EnumDisplayMonitors, NULL, &clip, T_GUEST_ADDR(enum_proc), 7), TRUE);
    T_EQ(g_calls, 0);

    T_CASE("a callback that returns 0 gives FALSE");
    g_calls = 0;
    g_ret = 0;
    T_EQ(CALL(user32, EnumDisplayMonitors, NULL, NULL, T_GUEST_ADDR(enum_proc), 7), FALSE);
    T_EQ(g_calls, 1);
    g_ret = 0xFFFFFFFF00000000;     /* a BOOL is its low 32 bits: the high half is not its value */
    T_EQ(CALL(user32, EnumDisplayMonitors, NULL, NULL, T_GUEST_ADDR(enum_proc), 7), FALSE);
    T_EQ(g_calls, 2);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    g_ret = 1;
    T_EQ(CALL(user32, EnumDisplayMonitors, NULL, NULL, T_GUEST_ADDR(enum_proc), 7), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    clip = (RECT){ 5000, 0, 6000, 10 };
    T_EQ(CALL(user32, EnumDisplayMonitors, NULL, &clip, T_GUEST_ADDR(enum_proc), 7), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the context of a shown window gives one call with that context and the part of the client area on the monitor, in the context's coordinates");
    HWND a = mk(WS_POPUP, NULL, 100, 50, 300, 200);         /* a popup has no frame: the client area is the window */
    T_REQUIRE(a != NULL);
    CALL(user32, ShowWindow, a, SW_SHOWDEFAULT);
    HDC da = (HDC)CALL(user32, GetDC, a);
    T_REQUIRE(da != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    g_calls = 0;
    g_ret = 1;
    T_EQ(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 0x55), TRUE);
    T_EQ(g_calls, 1);
    T_EQ(g_rcx, 0x10001);
    T_EQ(g_rdx, (uint64_t)(uintptr_t)da);
    T_EQ(g_rect[0], 0);
    T_EQ(g_rect[1], 0);
    T_EQ(g_rect[2], 300);
    T_EQ(g_rect[3], 200);
    T_EQ(g_data, 0x55);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the screen's context gives one call with that context and the monitor rectangle");
    HDC ds = (HDC)CALL(user32, GetDC, NULL);
    T_REQUIRE(ds != NULL);
    g_calls = 0;
    T_EQ(CALL(user32, EnumDisplayMonitors, ds, NULL, T_GUEST_ADDR(enum_proc), 7), TRUE);
    T_EQ(g_calls, 1);
    T_EQ(g_rdx, (uint64_t)(uintptr_t)ds);
    T_EQ(g_rect[0], 0);
    T_EQ(g_rect[1], 0);
    T_EQ(g_rect[2], 1920);
    T_EQ(g_rect[3], 1080);

    T_CASE("with a context too, a callback that returns 0 gives FALSE");
    g_calls = 0;
    g_ret = 0;
    T_EQ(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 7), FALSE);
    T_EQ(g_calls, 1);
    g_ret = 1;

    /* RT-TRAPS with a context: one that GetDC did not give out (a number, a memory context, a
     * released one), a clip, a moved viewport origin, a callback of 0. */
    T_TRAPS(CALL(user32, EnumDisplayMonitors, (HDC)(uintptr_t)0x1234, NULL, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with a device context that GetDC did not give out");
    HDC dm = rt_dc_create_compatible(NULL);
    T_REQUIRE(dm != NULL);
    T_TRAPS(CALL(user32, EnumDisplayMonitors, dm, NULL, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with a device context that GetDC did not give out");
    clip = (RECT){ 0, 0, 10, 10 };
    T_TRAPS(CALL(user32, EnumDisplayMonitors, da, &clip, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with a device context and a clip");
    T_TRAPS(CALL(user32, EnumDisplayMonitors, da, NULL, 0, 7), "EnumDisplayMonitors with lpfnEnum 0");
    T_TRUE(rt_dc_set_point(da, RT_DC_VIEWPORT_ORG, 3, 4, NULL));
    T_TRAPS(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with a device context whose viewport origin is 3,4");
    T_TRUE(rt_dc_set_point(da, RT_DC_VIEWPORT_ORG, 0, 0, NULL));

    /* The window moved so that it is partly beyond the monitor's right and bottom edges (120 x
     * 80 of its 300 x 200 are on it), then partly beyond the left and top edges (the part on
     * the monitor then begins at 50, 20 of the context); then wholly beyond it, which stops. */
    const UINT move = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    T_TRUE(rt_window_set_pos(T_CTX(), a, NULL, 1800, 1000, 0, 0, move));
    g_calls = 0;
    T_EQ(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 7), TRUE);
    T_EQ(g_calls, 1);
    T_EQ(g_rdx, (uint64_t)(uintptr_t)da);
    T_EQ(g_rect[0], 0);
    T_EQ(g_rect[1], 0);
    T_EQ(g_rect[2], 120);
    T_EQ(g_rect[3], 80);
    T_TRUE(rt_window_set_pos(T_CTX(), a, NULL, -50, -20, 0, 0, move));
    g_calls = 0;
    T_EQ(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 7), TRUE);
    T_EQ(g_calls, 1);
    T_EQ(g_rect[0], 50);
    T_EQ(g_rect[1], 20);
    T_EQ(g_rect[2], 300);
    T_EQ(g_rect[3], 200);
    T_TRUE(rt_window_set_pos(T_CTX(), a, NULL, 3000, 3000, 0, 0, move));
    T_TRAPS(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with the device context of a window whose client area is off the monitor");
    T_TRUE(rt_window_set_pos(T_CTX(), a, NULL, 100, 50, 0, 0, move));

    /* The window with a child; then a newer window, never shown: its own context stops for a
     * window that is not visible, and the first window is no longer the top one. */
    HWND c = mk(WS_CHILD, a, 0, 0, 10, 10);
    T_REQUIRE(c != NULL);
    T_TRAPS(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with the device context of a window that has a child");
    HWND hid = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(hid != NULL);
    HDC dh = (HDC)CALL(user32, GetDC, hid);
    T_REQUIRE(dh != NULL);
    T_TRAPS(CALL(user32, EnumDisplayMonitors, dh, NULL, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with the device context of a window that is not visible");
    T_TRAPS(CALL(user32, EnumDisplayMonitors, da, NULL, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with the device context of a window that is not the top one");

    /* RT-TRAPS without a context: a callback of 0, and a clip that touches the monitor's edge
     * or is empty: the documentation does not settle those, so the run stops with the values. */
    T_TRAPS(CALL(user32, EnumDisplayMonitors, NULL, NULL, 0, 7), "EnumDisplayMonitors with lpfnEnum 0");
    clip = (RECT){ 1920, 0, 2000, 10 };
    T_TRAPS(CALL(user32, EnumDisplayMonitors, NULL, &clip, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with a clip that touches or is empty");
    clip = (RECT){ 10, 10, 10, 10 };
    T_TRAPS(CALL(user32, EnumDisplayMonitors, NULL, &clip, T_GUEST_ADDR(enum_proc), 7),
            "EnumDisplayMonitors with a clip that touches or is empty");
}
