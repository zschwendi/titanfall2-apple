#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_window_get_client_rect ({0, 0, w, h} of the window rectangle
 * less its frame; a WS_POPUP has no frame, so the client area is {0, 0, 300, 200}; RT_HWND_DESKTOP
 * gives the monitor rectangle, from memory).  Invalid handles: review V27 (Wine user32/tests/win.c). */
GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

TEST(user32, GetClientRect)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(T_CTX(), &a);
    T_REQUIRE(h != NULL);
    RECT r;

    T_CASE("the client area: the window rectangle less its frame, from 0, 0");
    CALL(kernel32, SetLastError, 0x1234);
    r = (RECT){ 7, 7, 7, 7 };
    T_EQ(CALL(user32, GetClientRect, h, &r), TRUE);
    T_EQ(r.left, 0);
    T_EQ(r.top, 0);
    T_EQ(r.right, 300);
    T_EQ(r.bottom, 200);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the desktop window gives the monitor rectangle");
    r = (RECT){ 7, 7, 7, 7 };
    T_EQ(CALL(user32, GetClientRect, RT_HWND_DESKTOP, &r), TRUE);
    T_EQ(r.left, 0);
    T_EQ(r.top, 0);
    T_EQ(r.right, 1920);
    T_EQ(r.bottom, 1080);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is no window: the rectangle is not written");
    r = (RECT){ 7, 7, 7, 7 };
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetClientRect, NULL, &r), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(CALL(user32, GetClientRect, (HWND)0x40000FF0, &r), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(rt_window_destroy(T_CTX(), h), TRUE);
    T_EQ(CALL(user32, GetClientRect, h, &r), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(r.left == 7 && r.top == 7 && r.right == 7 && r.bottom == 7);

    T_CASE("does not change last error");
    h = rt_window_create(T_CTX(), &a);
    T_REQUIRE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetClientRect, h, &r), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(user32, GetClientRect, h, NULL), "rt_window_get_client_rect: not implemented");
}
