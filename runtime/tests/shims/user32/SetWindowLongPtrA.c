#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }
GUEST_FN(proc2) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }

/* An ANSI window (class registered with unicode false) and a wide one, with 16 extra bytes. */
static HWND make_window(bool unicode, DWORD style, DWORD exstyle)
{
    RtWindowCreate a = { .exstyle = exstyle, .style = style,
                         .cls = unicode ? RT_TEXT_W(u"TW") : RT_TEXT_A("TA"),
                         .title = unicode ? RT_TEXT_W(u"Title") : RT_TEXT_A("Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = unicode };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, SetWindowLongPtrA)
{
    /* An ANSI class: the name is narrow text, as the core reads it when unicode is false. */
    WNDCLASSEXW wa = { .cbSize = sizeof wa, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = (const WCHAR *)"TA", .cbWndExtra = 16 };
    WNDCLASSEXW wu = { .cbSize = sizeof wu, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"TW", .cbWndExtra = 16 };
    rt_wndclass_register(&wa, false);
    rt_wndclass_register(&wu, true);

    HWND a = make_window(false, WS_POPUP, 0);
    HWND w = make_window(true, WS_POPUP, 0);
    HWND gone = make_window(false, WS_POPUP, 0);
    rt_window_destroy(T_CTX(), gone);
    LONG_PTR v, before, style;

    T_CASE("stores a 64-bit user data and returns the previous value");
    rt_window_set_long(a, GWLP_USERDATA, (LONG_PTR)0x0102030405060708ULL, 8, false);
    v = CALL(user32, SetWindowLongPtrA, a, GWLP_USERDATA, (LONG_PTR)0x8000000000000001ULL);
    T_EQ((uint64_t)v, 0x0102030405060708ULL);
    T_EQ((uint64_t)rt_window_get_long(a, GWLP_USERDATA, 8, false), 0x8000000000000001ULL);

    T_CASE("stores 8 extra bytes at offset 8 and returns the previous value");
    rt_window_set_long(a, 8, (LONG_PTR)0x1111ULL, 8, false);
    v = CALL(user32, SetWindowLongPtrA, a, 8, (LONG_PTR)0xFEDCBA9876543210ULL);
    T_EQ((uint64_t)v, 0x1111ULL);
    T_EQ((uint64_t)rt_window_get_long(a, 8, 8, false), 0xFEDCBA9876543210ULL);

    T_CASE("fails with ERROR_INVALID_INDEX for an index past the extra bytes or an unknown negative index, changing nothing");
    before = rt_window_get_long(a, 8, 8, false);
    CALL(kernel32, SetLastError, 0);
    v = CALL(user32, SetWindowLongPtrA, a, 9, 0x55);       /* 9 + 8 > 16 */
    T_EQ(v, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    CALL(kernel32, SetLastError, 0);
    v = CALL(user32, SetWindowLongPtrA, a, -2, 0x55);
    T_EQ(v, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    T_EQ((uint64_t)rt_window_get_long(a, 8, 8, false), (uint64_t)before);

    T_CASE("sets the window procedure of an A window and returns the previous one");
    v = CALL(user32, SetWindowLongPtrA, a, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(proc2));
    T_EQ((uint64_t)v, (uint64_t)T_GUEST_ADDR(proc));
    T_EQ((uint64_t)rt_window_get_long(a, GWLP_WNDPROC, 8, false), (uint64_t)T_GUEST_ADDR(proc2));

    T_CASE("a procedure value of 0 changes nothing and returns the procedure");
    v = CALL(user32, SetWindowLongPtrA, a, GWLP_WNDPROC, 0);
    T_EQ((uint64_t)v, (uint64_t)T_GUEST_ADDR(proc2));
    T_EQ((uint64_t)rt_window_get_long(a, GWLP_WNDPROC, 8, false), (uint64_t)T_GUEST_ADDR(proc2));

    T_CASE("sets the style when that does not show the window or change its frame");
    style = rt_window_get_long(a, GWL_STYLE, 8, false);
    v = CALL(user32, SetWindowLongPtrA, a, GWL_STYLE, style | WS_CLIPCHILDREN);
    T_EQ(v, style);
    T_EQ(rt_window_get_long(a, GWL_STYLE, 8, false), style | WS_CLIPCHILDREN);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, SetWindowLongPtrA, a, GWLP_USERDATA, 7);  /* previous value nonzero */
    T_EQ(T_LAST_ERROR(), 0x1234);
    v = CALL(user32, SetWindowLongPtrA, a, GWLP_USERDATA, 5);   /* previous value 7 */
    T_EQ(v, 7);
    v = CALL(user32, SetWindowLongPtrA, a, GWLP_USERDATA, 0);   /* previous value 5 */
    CALL(kernel32, SetLastError, 0x1234);
    v = CALL(user32, SetWindowLongPtrA, a, GWLP_USERDATA, 9);   /* previous value 0: a success that is 0 */
    T_EQ(v, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("returns 0 for a handle that is not a window");
    v = CALL(user32, SetWindowLongPtrA, (HWND)0, GWLP_USERDATA, 1);
    T_EQ(v, 0);
    v = CALL(user32, SetWindowLongPtrA, (HWND)0x40000FF0, GWLP_USERDATA, 1);
    T_EQ(v, 0);
    v = CALL(user32, SetWindowLongPtrA, gone, GWLP_USERDATA, 1);
    T_EQ(v, 0);

    T_TRAPS(CALL(user32, SetWindowLongPtrA, a, GWLP_HWNDPARENT, 0), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrA, w, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(proc2)), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrA, a, GWL_STYLE, (LONG_PTR)(WS_POPUP | WS_VISIBLE)), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrA, a, GWL_STYLE, (LONG_PTR)(WS_POPUP | WS_CAPTION)), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrA, a, GWL_EXSTYLE, (LONG_PTR)WS_EX_CLIENTEDGE), "rt_window_set_long: not implemented");
    T_TRAPS(CALL(user32, SetWindowLongPtrA, RT_HWND_DESKTOP, GWL_STYLE, 0), "rt_window_set_long: not implemented");
}
