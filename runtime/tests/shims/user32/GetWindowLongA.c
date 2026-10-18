#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: the comments of rt_window_get_long and rt_window_set_long in rt/window.h,
 * which name Wine user32/tests/win.c (test_desktop, test_hwnd_message, test_set_window_long_size)
 * and review C4, V14, V29.  Documented: a window whose extra bytes were never set reads zero
 * (Microsoft's GetWindowLong page).  The window's procedure is a stub: it is never called here. */
GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

__attribute__((unused)) static HWND mk(DWORD style)
{
    static bool reg;
    if (!reg) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .style = style, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, GetWindowLongA)
{
    HWND h = mk(WS_POPUP);
    T_REQUIRE(h != NULL);

    T_CASE("the style given at creation");   /* and WS_CLIPSIBLINGS, which a top-level window gets */
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, GWL_STYLE), WS_POPUP | WS_CLIPSIBLINGS);

    T_CASE("the user data, the low 32 bits sign-extended");
    rt_window_set_long(h, GWLP_USERDATA, ((LONG_PTR)1 << 32) | 123, 8, true);
    T_EQ(CALL(user32, GetWindowLongA, h, GWLP_USERDATA), 123);
    rt_window_set_long(h, GWLP_USERDATA, -2, 8, true);
    T_EQ(CALL(user32, GetWindowLongA, h, GWLP_USERDATA), -2);

    T_CASE("extra bytes at offsets 0 and 12 of 16");
    T_EQ(CALL(user32, GetWindowLongA, h, 0), 0);
    T_EQ(CALL(user32, GetWindowLongA, h, 12), 0);
    rt_window_set_long(h, 0, 0x11223344, 4, true);
    rt_window_set_long(h, 12, (LONG_PTR)(LONG)0x80000001, 4, true);
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, 0), 0x11223344u);
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, 12), 0x80000001u);
    T_EQ(T_LAST_ERROR(), 0);

    T_CASE("an index beyond the extra bytes");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, h, 13), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);

    T_CASE("index -99");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, h, -99), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);

    T_CASE("the pointer indexes GWLP_WNDPROC, GWLP_HINSTANCE and GWLP_HWNDPARENT are refused");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, h, GWLP_WNDPROC), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, h, GWLP_HINSTANCE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, h, GWLP_HWNDPARENT), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);

    T_CASE("a success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, GWL_STYLE), WS_POPUP | WS_CLIPSIBLINGS);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is no window");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, NULL, GWL_STYLE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, (HWND)0x40000FF0, GWL_STYLE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    rt_window_destroy(T_CTX(), h);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, GetWindowLongA, h, GWL_STYLE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_TRAPS(CALL(user32, GetWindowLongA, RT_HWND_DESKTOP, GWL_STYLE), "rt_window_get_long: not implemented");
}
