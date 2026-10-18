#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: the comment of rt_window_show in rt/window.h, which names its source for
 * each statement (docs/reviews/win-window-sequences.md, S6 to S9). */
static UINT seen[32];
static unsigned nseen;

/* A window procedure that records the message numbers and passes everything on. */
GUEST_FN(rec)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    if (nseen < 32) seen[nseen++] = msg;
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
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rec), .lpszClassName = u"T", .cbWndExtra = 16 };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .style = style, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = x, .y = y, .cx = cx, .cy = cy, .parent = parent, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(user32, ShowWindow)
{
    HWND h = mk(WS_POPUP, NULL, 10, 20, 300, 200);
    T_REQUIRE(h != NULL);

    T_CASE("SW_HIDE on a hidden window: FALSE, nothing sent");
    nseen = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, ShowWindow, h, 0), FALSE);
    T_EQ(nseen, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(!rt_window_is_visible(h));

    T_CASE("SW_SHOWDEFAULT on a hidden window: shows and activates it");
    nseen = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, ShowWindow, h, 10), FALSE);
    T_TRUE(SAW(0x18, 0x46, 0x1C, 0x86, 0x06, 0x07, 0x47));
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(rt_window_is_visible(h));
    T_TRUE(rt_window_active() == h);

    T_CASE("SW_SHOWNORMAL, SW_SHOW, SW_SHOWDEFAULT on a shown window: nonzero, nothing sent");
    nseen = 0;
    T_TRUE(CALL(user32, ShowWindow, h, 1) != FALSE);
    T_TRUE(CALL(user32, ShowWindow, h, 5) != FALSE);
    T_TRUE(CALL(user32, ShowWindow, h, 10) != FALSE);
    T_EQ(nseen, 0);

    T_CASE("SW_HIDE on the shown window: nonzero, hides and deactivates it");
    nseen = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(user32, ShowWindow, h, 0) != FALSE);
    T_TRUE(SAW(0x18, 0x46, 0x47, 0x86, 0x06, 0x1C, 0x08));
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(!rt_window_is_visible(h));
    T_TRUE(rt_window_active() == NULL);

    T_CASE("a handle that is no window");
    T_EQ(CALL(user32, ShowWindow, NULL, 10), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, ShowWindow, (HWND)0x40000FF0, 10), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    HWND gone = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(gone != NULL);
    rt_window_destroy(T_CTX(), gone);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, ShowWindow, gone, 10), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    /* STOPS in the core: each is a fatal report, so they follow the last case. */
    T_TRAPS(CALL(user32, ShowWindow, h, 3), "rt_window_show: not implemented");
    HWND parent = mk(WS_POPUP, NULL, 0, 0, 50, 50);
    T_REQUIRE(parent != NULL);
    HWND child = mk(WS_CHILD, parent, 0, 0, 5, 5);
    T_REQUIRE(child != NULL);
    T_TRAPS(CALL(user32, ShowWindow, child, 10), "rt_window_show: not implemented");
    HWND other = mk(WS_POPUP, NULL, 0, 0, 40, 40);
    T_REQUIRE(other != NULL);
    T_EQ(CALL(user32, ShowWindow, other, 10), FALSE);
    T_TRAPS(CALL(user32, ShowWindow, mk(WS_POPUP, NULL, 0, 0, 40, 40), 10), "rt_window_show: not implemented");
}
