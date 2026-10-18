#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_window_client_to_screen (the client area's corner is the
 * window rectangle's corner plus the frame; a WS_POPUP has no frame, so (10, 20) is the
 * client origin).  Invalid handles: review V27 (Wine user32/tests/win.c, test_map_points). */
GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

TEST(user32, ClientToScreen)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(T_CTX(), &a);
    T_REQUIRE(h != NULL);
    POINT p;

    T_CASE("adds the client area's screen position to a point");
    CALL(kernel32, SetLastError, 0x1234);
    p = (POINT){ 5, 6 };
    T_EQ(CALL(user32, ClientToScreen, h, &p), TRUE);
    T_EQ(p.x, 15);
    T_EQ(p.y, 26);
    T_EQ(T_LAST_ERROR(), 0x1234);
    p = (POINT){ 0, 0 };
    T_EQ(CALL(user32, ClientToScreen, h, &p), TRUE);
    T_EQ(p.x, 10);
    T_EQ(p.y, 20);

    T_CASE("the desktop window changes nothing");
    p = (POINT){ 5, 6 };
    T_EQ(CALL(user32, ClientToScreen, RT_HWND_DESKTOP, &p), TRUE);
    T_EQ(p.x, 5);
    T_EQ(p.y, 6);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is no window: the point is not written");
    p = (POINT){ 5, 6 };
    T_EQ(CALL(user32, ClientToScreen, NULL, &p), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(CALL(user32, ClientToScreen, (HWND)0x40000FF0, &p), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(p.x == 5 && p.y == 6);
    T_EQ(rt_window_destroy(T_CTX(), h), TRUE);
    T_EQ(CALL(user32, ClientToScreen, h, &p), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(p.x == 5 && p.y == 6);

    T_CASE("does not change last error");
    h = rt_window_create(T_CTX(), &a);
    T_REQUIRE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    p = (POINT){ 5, 6 };
    T_EQ(CALL(user32, ClientToScreen, h, &p), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(user32, ClientToScreen, h, NULL), "a NULL point");
}
