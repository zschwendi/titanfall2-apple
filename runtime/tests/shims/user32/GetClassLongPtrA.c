#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/user.h"

GUEST_FN(proc) { ctx->r[0] = ((UINT)ctx->r[2] == WM_NCCREATE); GUEST_RET(ctx); }

/* A window of a class registered with cbWndExtra 16 and hCursor 0x5151; the class's atom is
 * the registration's return value. */
static HWND mk(ATOM *atom)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(proc), .lpszClassName = u"T",
                       .cbWndExtra = 16, .hCursor = (HCURSOR)(uintptr_t)0x5151 };
    *atom = rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, GetClassLongPtrA)
{
    ATOM atom;
    HWND h = mk(&atom);

    T_CASE("returns the class field of a window: GCLP_HCURSOR as registered");
    T_EQ(CALL(user32, GetClassLongPtrA, h, GCLP_HCURSOR), (ULONG_PTR)0x5151);

    T_CASE("GCW_ATOM is the atom of the registration");
    T_EQ(CALL(user32, GetClassLongPtrA, h, GCW_ATOM), (ULONG_PTR)atom);

    T_CASE("GCL_CBWNDEXTRA is the registered extra size (16)");
    T_EQ(CALL(user32, GetClassLongPtrA, h, GCL_CBWNDEXTRA), (ULONG_PTR)16);

    T_CASE("index -99 fails with ERROR_INVALID_INDEX");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetClassLongPtrA, h, -99), (ULONG_PTR)0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);

    T_CASE("an invalid window gives 0");
    T_EQ(CALL(user32, GetClassLongPtrA, NULL, GCLP_HCURSOR), (ULONG_PTR)0);
    T_EQ(CALL(user32, GetClassLongPtrA, (HWND)0x40000FF0, GCLP_HCURSOR), (ULONG_PTR)0);
    T_EQ(rt_window_destroy(T_CTX(), h), TRUE);
    T_EQ(CALL(user32, GetClassLongPtrA, h, GCLP_HCURSOR), (ULONG_PTR)0);

    T_CASE("success leaves last error alone");
    h = mk(&atom);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetClassLongPtrA, h, GCLP_HCURSOR), (ULONG_PTR)0x5151);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops: each ends the run with its fatal report. */
    T_TRAPS(CALL(user32, GetClassLongPtrA, h, GCLP_MENUNAME), "rt_wndclass_get_long: not implemented");
    T_TRAPS(CALL(user32, GetClassLongPtrA, RT_HWND_DESKTOP, GCLP_WNDPROC), "rt_wndclass_get_long: not implemented");
}
