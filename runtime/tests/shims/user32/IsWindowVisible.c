#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: the comments of the core functions in rt/window.h and rt/gdi.h, which name
 * their source for each statement (docs/reviews/win-window-sequences.md and the two contract
 * reviews).  The piece's own tests are tests/core/win_window.c and tests/core/gdi_dc.c. */
static UINT seen[32];
static unsigned nseen;
static WPARAM last_wp;
static LPARAM last_lp;

/* A window procedure that records the message numbers and passes everything on. */
GUEST_FN(rec)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    if (nseen < 32) seen[nseen++] = msg;
    last_wp = wp; last_lp = lp;
    tf_fp_enter_host();
    const LRESULT r = rt_window_def_proc(ctx, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}
__attribute__((unused)) static bool saw(const UINT *want, unsigned n)
{
    if (nseen != n) return false;
    for (unsigned i = 0; i < n; i++) if (seen[i] != want[i]) return false;
    return true;
}
#define SAW(...)  saw((const UINT[]){ __VA_ARGS__ }, sizeof((const UINT[]){ __VA_ARGS__ }) / sizeof(UINT))
__attribute__((unused)) static HWND mk(DWORD style, HWND parent, INT x, INT y, INT cx, INT cy)
{
    static bool reg;
    if (!reg) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rec), .lpszClassName = u"T" };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .style = style, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"), .x = x, .y = y, .cx = cx, .cy = cy,
                         .parent = parent, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, IsWindowVisible)
{
    HWND h = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(h != NULL);
    HWND kid = mk(WS_CHILD, h, 0, 0, 5, 5);
    HWND gone = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(gone != NULL);
    T_REQUIRE(rt_window_destroy(T_CTX(), gone));

    T_CASE("a new record");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, IsWindowVisible, h), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("after the window is shown");
    rt_window_show(h, 5);
    T_EQ(CALL(user32, IsWindowVisible, h), TRUE);
    T_EQ(CALL(user32, IsWindowVisible, kid), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the desktop window");
    T_EQ(CALL(user32, IsWindowVisible, RT_HWND_DESKTOP), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is no window");
    T_EQ(CALL(user32, IsWindowVisible, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, IsWindowVisible, (HWND)0x40000FF0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, IsWindowVisible, gone), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
}
