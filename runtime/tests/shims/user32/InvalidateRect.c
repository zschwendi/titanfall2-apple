#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

static HWND mk(void)
{
    static bool reg;
    if (!reg) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, InvalidateRect)
{
    HWND h = mk();
    T_REQUIRE(h != NULL);
    RECT rc = { 1, 2, 30, 40 };

    T_CASE("a record with a rectangle and with NULL, erase TRUE or FALSE");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, InvalidateRect, h, &rc, TRUE), TRUE);
    T_EQ(CALL(user32, InvalidateRect, h, &rc, FALSE), TRUE);
    T_EQ(CALL(user32, InvalidateRect, h, NULL, TRUE), TRUE);
    T_EQ(CALL(user32, InvalidateRect, h, NULL, FALSE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a NULL window invalidates all windows");
    T_EQ(CALL(user32, InvalidateRect, NULL, &rc, TRUE), TRUE);
    T_EQ(CALL(user32, InvalidateRect, NULL, NULL, FALSE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is no window");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, InvalidateRect, (HWND)0x40000FF0, NULL, TRUE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    HWND gone = mk();
    T_REQUIRE(gone != NULL);
    rt_window_destroy(T_CTX(), gone);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, InvalidateRect, gone, &rc, FALSE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
}
