#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }

/* An ANSI window (class registered with unicode false) and a wide one, with 16 extra bytes. */
static HWND make_window(bool unicode, DWORD style, DWORD exstyle)
{
    RtWindowCreate a = { .exstyle = exstyle, .style = style,
                         .cls = unicode ? RT_TEXT_W(u"TW") : RT_TEXT_A("TA"),
                         .title = unicode ? RT_TEXT_W(u"Title") : RT_TEXT_A("Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = unicode };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, GetWindowLongPtrA)
{
    /* An ANSI class: the name is narrow text, as the core reads it when unicode is false. */
    WNDCLASSEXW wa = { .cbSize = sizeof wa, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = (const WCHAR *)"TA", .cbWndExtra = 16 };
    WNDCLASSEXW wu = { .cbSize = sizeof wu, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"TW", .cbWndExtra = 16 };
    rt_wndclass_register(&wa, false);
    rt_wndclass_register(&wu, true);

    HWND a = make_window(false, WS_POPUP, WS_EX_TOPMOST);
    HWND w = make_window(true, WS_POPUP, 0);
    HWND gone = make_window(false, WS_POPUP, 0);
    RtWindowCreate ca = { .style = WS_CHILD, .cls = RT_TEXT_A("TA"), .title = RT_TEXT_A("C"),
                          .x = 1, .y = 2, .cx = 50, .cy = 40, .parent = a, .menu = (HMENU)(uintptr_t)42,
                          .unicode = false };
    HWND child = rt_window_create(T_CTX(), &ca);
    LONG_PTR v;
    rt_window_destroy(T_CTX(), gone);

    T_CASE("reads the user data as a 64-bit value");
    rt_window_set_long(a, GWLP_USERDATA, (LONG_PTR)0x1122334455667788ULL, 8, false);
    v = CALL(user32, GetWindowLongPtrA, a, GWLP_USERDATA);
    T_EQ((uint64_t)v, 0x1122334455667788ULL);

    T_CASE("reads 8 extra bytes at offset 8 of a 16-byte area whole");
    rt_window_set_long(a, 8, (LONG_PTR)0x8000000000000001ULL, 8, false);
    v = CALL(user32, GetWindowLongPtrA, a, 8);
    T_EQ((uint64_t)v, 0x8000000000000001ULL);

    T_CASE("fails with ERROR_INVALID_INDEX for an index past the extra bytes or an unknown negative index");
    CALL(kernel32, SetLastError, 0);
    v = CALL(user32, GetWindowLongPtrA, a, 9);             /* 9 + 8 > 16 */
    T_EQ(v, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    CALL(kernel32, SetLastError, 0);
    v = CALL(user32, GetWindowLongPtrA, a, -2);
    T_EQ(v, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);

    T_CASE("reads the style, extended style and identifier");
    v = CALL(user32, GetWindowLongPtrA, a, GWL_STYLE);
    T_EQ((DWORD)v & WS_POPUP, WS_POPUP);               /* the core may add its own bits */
    T_EQ(v, rt_window_get_long(a, GWL_STYLE, 8, false));
    v = CALL(user32, GetWindowLongPtrA, a, GWL_EXSTYLE);
    T_EQ((DWORD)v, WS_EX_TOPMOST);
    v = CALL(user32, GetWindowLongPtrA, child, GWL_ID);
    T_EQ(v, 42);

    T_CASE("reads the window procedure of an A window");
    v = CALL(user32, GetWindowLongPtrA, a, GWLP_WNDPROC);
    T_EQ((uint64_t)v, (uint64_t)T_GUEST_ADDR(proc));

    T_CASE("reads the parent of a child and 0 for a top-level window");
    v = CALL(user32, GetWindowLongPtrA, child, GWLP_HWNDPARENT);
    T_EQ((uint64_t)v, (uint64_t)(uintptr_t)a);
    v = CALL(user32, GetWindowLongPtrA, a, GWLP_HWNDPARENT);
    T_EQ(v, 0);

    T_CASE("returns 0 for a handle that is not a window");
    v = CALL(user32, GetWindowLongPtrA, (HWND)0, GWL_STYLE);
    T_EQ(v, 0);
    v = CALL(user32, GetWindowLongPtrA, (HWND)0x40000FF0, GWL_STYLE);
    T_EQ(v, 0);
    v = CALL(user32, GetWindowLongPtrA, gone, GWL_STYLE);
    T_EQ(v, 0);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetWindowLongPtrA, a, GWLP_USERDATA);     /* a success */
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    v = CALL(user32, GetWindowLongPtrA, a, GWLP_HWNDPARENT);   /* a success that is 0 */
    T_EQ(v, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(user32, GetWindowLongPtrA, w, GWLP_WNDPROC), "rt_window_get_long: not implemented");
    T_TRAPS(CALL(user32, GetWindowLongPtrA, RT_HWND_DESKTOP, GWL_STYLE), "rt_window_get_long: not implemented");
}
