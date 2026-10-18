#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/user.h"

/* The window procedure: WM_USER answers twice wParam; WM_NCCREATE answers TRUE so that the
 * window can be created; every other message is answered 0. */
GUEST_FN(proc)
{
    UINT msg = (UINT)ctx->r[2];
    if (msg == WM_USER) ctx->r[0] = (WPARAM)ctx->r[8] * 2;
    else ctx->r[0] = (msg == WM_NCCREATE);
    GUEST_RET(ctx);
}

static HWND make_window(void)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, SendMessageA)
{
    HWND h;
    LRESULT r;

    T_CASE("sends a message to a window of the calling thread and returns the procedure's result");
    h = make_window();
    T_TRUE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(user32, SendMessageA, h, WM_USER, 21, 0);
    T_EQ(r, 42);
    T_EQ(T_LAST_ERROR(), 0x1234);      /* success leaves last error alone */
    r = CALL(user32, SendMessageA, h, WM_USER, 0, 0);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    rt_window_destroy(T_CTX(), h);

    T_CASE("an invalid window returns 0 with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    r = CALL(user32, SendMessageA, NULL, WM_USER, 1, 0);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    r = CALL(user32, SendMessageA, (HWND)0x40000FF0, WM_USER, 1, 0);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    h = make_window();
    T_TRUE(h != NULL);
    rt_window_destroy(T_CTX(), h);
    CALL(kernel32, SetLastError, 0);
    r = CALL(user32, SendMessageA, h, WM_USER, 1, 0);
    T_EQ(r, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_TRAPS(CALL(user32, SendMessageA, HWND_BROADCAST, WM_USER, 1, 0), "rt_window_send: not implemented");
    T_TRAPS(CALL(user32, SendMessageA, RT_HWND_DESKTOP, WM_USER, 1, 0), "rt_window_send: not implemented");
    h = make_window();
    T_TRUE(h != NULL);
    T_TRAPS(CALL(user32, SendMessageA, h, WM_SETTEXT, 0, (LPARAM)"x"), "rt_window_send: not implemented");
    rt_window_destroy(T_CTX(), h);
}
