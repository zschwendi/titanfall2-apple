#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: the comments of rt_window_get_long and rt_window_set_long in rt/window.h,
 * which name Wine user32/tests/win.c (test_desktop, test_hwnd_message, test_set_window_long_size,
 * test_SetWindowLong, test_window_styles) and review C4, V14, V29, V30.  The window's procedure
 * is a stub that answers WM_NCCREATE and is never called here. */
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

TEST(user32, SetWindowLongA)
{
    HWND h = mk(WS_POPUP);
    T_REQUIRE(h != NULL);
    const DWORD style0 = (DWORD)CALL(user32, GetWindowLongA, h, GWL_STYLE);

    T_CASE("the style, the extended style and the user data set and returned");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((DWORD)CALL(user32, SetWindowLongA, h, GWL_STYLE, style0 | WS_DISABLED), style0);
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, GWL_STYLE), style0 | WS_DISABLED);
    T_EQ((DWORD)CALL(user32, SetWindowLongA, h, GWL_EXSTYLE, WS_EX_TOOLWINDOW), 0);
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, GWL_EXSTYLE), WS_EX_TOOLWINDOW);
    T_EQ(CALL(user32, SetWindowLongA, h, GWLP_USERDATA, 0x1234567), 0);
    T_EQ(CALL(user32, GetWindowLongA, h, GWLP_USERDATA), 0x1234567);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the previous value of a set");
    rt_window_set_long(h, GWLP_USERDATA, ((LONG_PTR)1 << 32) | 123, 8, true);
    T_EQ(CALL(user32, SetWindowLongA, h, GWLP_USERDATA, 7), 123);
    T_EQ(CALL(user32, GetWindowLongA, h, GWLP_USERDATA), 7);

    T_CASE("extra bytes at offsets 0 and 12 of 16");
    T_EQ(CALL(user32, SetWindowLongA, h, 0, 0x11223344), 0);
    T_EQ(CALL(user32, SetWindowLongA, h, 12, (LONG)0x80000001), 0);
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, 0), 0x11223344u);
    T_EQ((DWORD)CALL(user32, GetWindowLongA, h, 12), 0x80000001u);
    T_EQ(CALL(user32, SetWindowLongA, h, 0, 5), 0x11223344);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("an index beyond the extra bytes");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, SetWindowLongA, h, 13, 0x55), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    T_EQ((DWORD)rt_window_get_long(h, 12, 4, true), 0x80000001u);

    T_CASE("index -99");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, SetWindowLongA, h, -99, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);

    T_CASE("the pointer indexes GWLP_WNDPROC, GWLP_HINSTANCE and GWLP_HWNDPARENT are refused");
    {
        const LONG_PTR proc0 = rt_window_get_long(h, GWLP_WNDPROC, 8, true);
        const LONG_PTR inst0 = rt_window_get_long(h, GWLP_HINSTANCE, 8, true);
        const LONG_PTR parent0 = rt_window_get_long(h, GWLP_HWNDPARENT, 8, true);
        T_EQ((uint64_t)proc0, T_GUEST_ADDR(proc));
        CALL(kernel32, SetLastError, 0xdeadbeef);
        T_EQ(CALL(user32, SetWindowLongA, h, GWLP_WNDPROC, 0x12345678), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        CALL(kernel32, SetLastError, 0xdeadbeef);
        T_EQ(CALL(user32, SetWindowLongA, h, GWLP_HINSTANCE, 0x12345678), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        CALL(kernel32, SetLastError, 0xdeadbeef);
        T_EQ(CALL(user32, SetWindowLongA, h, GWLP_HWNDPARENT, 0x12345678), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        T_EQ(rt_window_get_long(h, GWLP_WNDPROC, 8, true), proc0);
        T_EQ(rt_window_get_long(h, GWLP_HINSTANCE, 8, true), inst0);
        T_EQ(rt_window_get_long(h, GWLP_HWNDPARENT, 8, true), parent0);
    }

    T_CASE("a set whose previous value is 0 leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetWindowLongA, h, 4, 9), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is no window");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, SetWindowLongA, NULL, GWL_STYLE, 0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, SetWindowLongA, (HWND)0x40000FF0, GWLP_USERDATA, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    rt_window_destroy(T_CTX(), h);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, SetWindowLongA, h, GWLP_USERDATA, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    /* The traps: each is a stop of rt_window_set_long that a test can reach. */
    HWND w = mk(WS_POPUP);
    T_REQUIRE(w != NULL);
    T_TRAPS(CALL(user32, SetWindowLongA, w, GWL_STYLE, WS_POPUP | WS_VISIBLE), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongA, w, GWL_STYLE, WS_POPUP | WS_CAPTION), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongA, w, GWL_EXSTYLE, WS_EX_WINDOWEDGE), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongA, RT_HWND_DESKTOP, GWL_STYLE, 0), "rt_window_set_long: not implemented");
}
