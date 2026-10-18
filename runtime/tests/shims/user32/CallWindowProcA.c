#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/gdi.h"

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

GUEST_FN(sum4) { ctx->r[0] = ctx->r[1] + (UINT)ctx->r[2] + ctx->r[8] + ctx->r[9]; GUEST_RET(ctx); }

TEST(user32, CallWindowProcA)
{
    T_CASE("the four arguments arrive in order and the 64-bit result comes back");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, CallWindowProcA, T_GUEST_ADDR(sum4), (HWND)0x1000, 0x20, 0x300, 0x500000000ll), 0x500001320ll);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRAPS(CALL(user32, CallWindowProcA, 0, NULL, 0, 0, 0), "rt_window_call_proc: not implemented");
}
