#include "rt/test.h"
#include "rt/window.h"

/* The window's procedure answers wParam + lParam to WM_USER, so the value the dispatch returns
 * is the procedure's answer; WM_NCCREATE gets 1 so that the window is created. */
GUEST_FN(proc)
{
    if ((UINT)ctx->r[2] == WM_USER) ctx->r[0] = (LRESULT)(ctx->r[8] + ctx->r[9]);
    else ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

TEST(user32, DispatchMessageW)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(T_CTX(), &a);
    HWND gone = rt_window_create(T_CTX(), &a);
    MSG m = { .hwnd = h, .message = WM_USER, .wParam = 3, .lParam = 4 };
    MSG n = { .hwnd = NULL, .message = WM_USER, .wParam = 3, .lParam = 4 };
    MSG f = { .hwnd = (HWND)0x40000FF0, .message = WM_USER, .wParam = 3, .lParam = 4 };
    MSG d = { .hwnd = gone, .message = WM_USER, .wParam = 3, .lParam = 4 };
    MSG t = { .hwnd = h, .message = WM_TIMER, .lParam = (LPARAM)T_GUEST_ADDR(proc) };

    T_CASE("a message for a window reaches its procedure");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, DispatchMessageW, &m), 7);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a message with no window returns 0");
    T_EQ(CALL(user32, DispatchMessageW, &n), 0);

    T_CASE("a handle that is no window fails with ERROR_INVALID_WINDOW_HANDLE");
    rt_window_destroy(T_CTX(), gone);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, DispatchMessageW, &f), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, DispatchMessageW, &d), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_TRAPS(CALL(user32, DispatchMessageW, NULL), "rt_window_dispatch: not implemented");
    T_TRAPS(CALL(user32, DispatchMessageW, &t), "rt_window_dispatch: not implemented");
}
