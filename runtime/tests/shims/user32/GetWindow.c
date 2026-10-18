#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

/* Two top-level windows A then B, and two children C then D of A, made through the core.  The
 * order is Windows': top-level windows newest first (B, A); children oldest first (C, D). */
TEST(user32, GetWindow)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate top = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                           .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    RtWindowCreate child = { .style = WS_CHILD, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Child"),
                             .x = 1, .y = 2, .cx = 30, .cy = 40, .unicode = true };
    HWND a, b, c, d, owned, gone;
    HWND f = (HWND)0x40000FF0;

    a = rt_window_create(T_CTX(), &top);
    b = rt_window_create(T_CTX(), &top);
    child.parent = a;
    c = rt_window_create(T_CTX(), &child);
    d = rt_window_create(T_CTX(), &child);
    gone = rt_window_create(T_CTX(), &top);
    rt_window_destroy(T_CTX(), gone);

    T_CASE("GW_HWNDNEXT and GW_HWNDPREV walk the top-level windows newest first");
    T_EQ(CALL(user32, GetWindow, b, GW_HWNDNEXT), a);
    T_EQ(CALL(user32, GetWindow, a, GW_HWNDPREV), b);
    T_EQ(CALL(user32, GetWindow, a, GW_HWNDNEXT), NULL);
    T_EQ(CALL(user32, GetWindow, b, GW_HWNDPREV), NULL);

    T_CASE("GW_CHILD is the first child created, and GW_HWNDNEXT walks the children in creation order");
    T_EQ(CALL(user32, GetWindow, a, GW_CHILD), c);
    T_EQ(CALL(user32, GetWindow, c, GW_HWNDNEXT), d);
    T_EQ(CALL(user32, GetWindow, d, GW_HWNDNEXT), NULL);

    T_CASE("GW_OWNER is the owner of an owned window and NULL for one with no owner");
    owned = rt_window_create(T_CTX(), &(RtWindowCreate){ .style = WS_POPUP, .cls = RT_TEXT_W(u"T"),
                             .title = RT_TEXT_W(u"Owned"), .x = 5, .y = 6, .cx = 70, .cy = 80,
                             .parent = a, .unicode = true });
    T_EQ(CALL(user32, GetWindow, owned, GW_OWNER), a);
    T_EQ(CALL(user32, GetWindow, a, GW_OWNER), NULL);

    T_CASE("GW_ENABLEDPOPUP is NULL and leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetWindow, a, GW_ENABLEDPOPUP), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("no such window is NULL and leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetWindow, b, GW_CHILD), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, GetWindow, a, GW_ENABLEDPOPUP), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, GetWindow, d, GW_HWNDNEXT), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("an invalid window is NULL with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetWindow, NULL, GW_HWNDNEXT), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetWindow, f, GW_OWNER), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetWindow, gone, GW_HWNDNEXT), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("a command of 7 or more is NULL with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetWindow, a, 77), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
}
