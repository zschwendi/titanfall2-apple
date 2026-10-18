#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }

/* The oracle is the window the test created itself: the child with identifier 42 is the
 * one GetDlgItem must return.  The core's comment (rt/window.h) names the codes. */
TEST(user32, GetDlgItem)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    HWND h = rt_window_create(T_CTX(), &a);
    RtWindowCreate c = { .style = WS_CHILD, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Child"),
                         .x = 1, .y = 1, .cx = 50, .cy = 20, .parent = h, .menu = (HMENU)(uintptr_t)42, .unicode = true };
    HWND child = rt_window_create(T_CTX(), &c);
    RtWindowCreate g = { .style = WS_CHILD, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Grandchild"),
                         .x = 1, .y = 1, .cx = 10, .cy = 10, .parent = child, .menu = (HMENU)(uintptr_t)43, .unicode = true };
    HWND grand = rt_window_create(T_CTX(), &g);

    T_CASE("returns the child whose identifier is the id");
    T_EQ(CALL(user32, GetDlgItem, h, 42), child);

    T_CASE("the children's own children are not searched");
    T_EQ(CALL(user32, GetDlgItem, h, 43), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_CONTROL_ID_NOT_FOUND);
    T_EQ(CALL(user32, GetDlgItem, child, 43), grand);

    T_CASE("an identifier no child has gives NULL and ERROR_CONTROL_ID_NOT_FOUND");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetDlgItem, h, 44), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_CONTROL_ID_NOT_FOUND);

    T_CASE("an invalid window gives NULL and ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetDlgItem, NULL, 42), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetDlgItem, (HWND)0x40000FF0, 42), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(rt_window_destroy(T_CTX(), h));
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetDlgItem, h, 42), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("success does not change last error");
    h = rt_window_create(T_CTX(), &a);
    c.parent = h;
    child = rt_window_create(T_CTX(), &c);
    T_TRUE(child != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetDlgItem, h, 42), child);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
