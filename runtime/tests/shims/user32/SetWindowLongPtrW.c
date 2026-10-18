#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

GUEST_FN(proc)
{
    ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE);
    GUEST_RET(ctx);
}

GUEST_FN(other)
{
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

TEST(user32, SetWindowLongPtrW)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T", .cbWndExtra = 16 };
    WNDCLASSEXA ca = { .cbSize = sizeof ca, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = "TA", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);
    rt_wndclass_register((const WNDCLASSEXW *)&ca, false);

    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    RtWindowCreate n = { .style = WS_POPUP, .cls = RT_TEXT_A("TA"), .title = RT_TEXT_A("ab"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = false };
    HWND h = rt_window_create(T_CTX(), &a);
    HWND ha = rt_window_create(T_CTX(), &n);
    T_REQUIRE(h != NULL);
    T_REQUIRE(ha != NULL);

    T_CASE("replacing the procedure returns the one before, and the next replacement returns the one replaced");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(other)), (ULONG_PTR)T_GUEST_ADDR(proc));
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, GetWindowLongPtrW, h, GWLP_WNDPROC), (ULONG_PTR)T_GUEST_ADDR(other));
    T_EQ(CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, 0), (ULONG_PTR)T_GUEST_ADDR(other));   /* 0 changes nothing */
    T_EQ(CALL(user32, GetWindowLongPtrW, h, GWLP_WNDPROC), (ULONG_PTR)T_GUEST_ADDR(other));
    T_EQ(CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(proc)), (ULONG_PTR)T_GUEST_ADDR(other));
    T_EQ(CALL(user32, GetWindowLongPtrW, h, GWLP_WNDPROC), (ULONG_PTR)T_GUEST_ADDR(proc));
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the user data and the extra bytes");
    CALL(user32, SetWindowLongPtrW, h, GWLP_USERDATA, 0);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetWindowLongPtrW, h, GWLP_USERDATA, (LONG_PTR)0x1122334455667788ull), 0);   /* previous 0 */
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, GetWindowLongPtrW, h, GWLP_USERDATA), (ULONG_PTR)0x1122334455667788ull);
    T_EQ(CALL(user32, SetWindowLongPtrW, h, GWLP_USERDATA, (LONG_PTR)0x1122334455667788ull), (ULONG_PTR)0x1122334455667788ull);
    CALL(user32, SetWindowLongPtrW, h, 8, (LONG_PTR)0x0102030405060708ull);
    T_EQ(CALL(user32, GetWindowLongPtrW, h, 8), (ULONG_PTR)0x0102030405060708ull);

    T_CASE("an index that does not exist");
    CALL(user32, SetWindowLongPtrW, h, 8, (LONG_PTR)0x0102030405060708ull);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowLongPtrW, h, 16, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowLongPtrW, h, 9, 1), 0);       /* 9 + 8 is beyond the 16 extra bytes */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowLongPtrW, h, -99, 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    T_EQ(CALL(user32, GetWindowLongPtrW, h, 8), (ULONG_PTR)0x0102030405060708ull);   /* nothing changed */

    T_CASE("a handle that is no window");
    HWND d = rt_window_create(T_CTX(), &a);
    T_REQUIRE(d != NULL);
    T_EQ(rt_window_destroy(T_CTX(), d), TRUE);
    T_EQ(CALL(user32, SetWindowLongPtrW, NULL, GWLP_USERDATA, 1), 0);
    T_EQ(CALL(user32, SetWindowLongPtrW, (HWND)0x40000FF0, GWLP_USERDATA, 1), 0);
    T_EQ(CALL(user32, SetWindowLongPtrW, d, GWLP_USERDATA, 1), 0);

    /* The traps: the core stops for these and the shim adds no guard. */
    T_TRAPS(CALL(user32, SetWindowLongPtrW, h, GWL_STYLE, WS_POPUP | WS_VISIBLE), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrW, h, GWL_STYLE, WS_POPUP | WS_CAPTION), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrW, h, GWL_STYLE, WS_POPUP | WS_THICKFRAME), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrW, h, GWLP_HWNDPARENT, (LONG_PTR)ha), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrW, RT_HWND_DESKTOP, GWL_STYLE, WS_POPUP), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrW, ha, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(other)), "rt_window_set_long: not implemented");
}
