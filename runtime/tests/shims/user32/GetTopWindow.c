#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

/* Two top-level windows A then B, and two children C then D of A, made through the core.  The
 * order is Windows': top-level windows newest first (B is the top); children oldest first (C). */
TEST(user32, GetTopWindow)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate top = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                           .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    RtWindowCreate child = { .style = WS_CHILD, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Child"),
                             .x = 1, .y = 2, .cx = 30, .cy = 40, .unicode = true };
    HWND a, b, c, d, gone;
    HWND f = (HWND)0x40000FF0;

    a = rt_window_create(T_CTX(), &top);
    b = rt_window_create(T_CTX(), &top);
    child.parent = a;
    c = rt_window_create(T_CTX(), &child);
    d = rt_window_create(T_CTX(), &child);
    gone = rt_window_create(T_CTX(), &top);
    rt_window_destroy(T_CTX(), gone);

    T_CASE("NULL and the desktop give the newest top-level window");
    T_EQ(CALL(user32, GetTopWindow, NULL), b);
    T_EQ(CALL(user32, GetTopWindow, RT_HWND_DESKTOP), b);
    T_EQ(CALL(user32, GetWindow, b, GW_HWNDNEXT), a);

    T_CASE("a parent's top window is its first child created");
    T_EQ(CALL(user32, GetTopWindow, a), c);
    T_EQ(CALL(user32, GetWindow, c, GW_HWNDNEXT), d);

    T_CASE("a window with no children and no such window are NULL and leave last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetTopWindow, b), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, GetTopWindow, c), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("an invalid window is NULL with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetTopWindow, f), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetTopWindow, gone), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
}
