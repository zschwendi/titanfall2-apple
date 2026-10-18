#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/user.h"

/* Expected values: the comments of rt_window_set_pos and rt_window_get_rect in rt/window.h,
 * which name their sources (Wine user32/tests/msg.c, WmSWP_ResizeSeq and WmSWP_MoveSeq; review D7).
 * Every call passes SWP_NOACTIVATE, so no window is activated. */
#define MAXSEEN 32
static UINT seen[MAXSEEN];
static unsigned nseen;
static UINT edit;      /* 0: none; 1: WM_WINDOWPOSCHANGING adds SWP_NOREDRAW; 2: it puts the window at the bottom */
static bool bare;      /* true: WM_NCCALCSIZE is answered 0 without the default procedure (for a frame it refuses) */

/* A window procedure that records the message numbers and passes everything to the default
 * procedure.  With `edit` set it changes the WINDOWPOS of WM_WINDOWPOSCHANGING. */
GUEST_FN(rec)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    if (nseen < MAXSEEN) seen[nseen++] = msg;
    if (msg == WM_NCCALCSIZE && bare) {
        ctx->r[0] = 0;
        GUEST_RET(ctx);
    }
    if (msg == WM_WINDOWPOSCHANGING && edit) {
        WINDOWPOS *pos = (WINDOWPOS *)(uintptr_t)lp;
        if (edit == 1) pos->flags |= SWP_NOREDRAW;
        else pos->hwndInsertAfter = HWND_BOTTOM;
    }
    tf_fp_enter_host();
    const LRESULT r = rt_window_def_proc(ctx, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

/* True if the messages recorded since the last reset are exactly the n given in want. */
static bool saw(const UINT *want, unsigned n)
{
    if (nseen != n) return false;
    for (unsigned i = 0; i < n; i++) if (seen[i] != want[i]) return false;
    return true;
}
#define SAW(...)  saw((const UINT[]){ __VA_ARGS__ }, sizeof((const UINT[]){ __VA_ARGS__ }) / sizeof(UINT))

/* True if `msg` was recorded since the last reset. */
static bool got(UINT msg)
{
    for (unsigned i = 0; i < nseen && i < MAXSEEN; i++) if (seen[i] == msg) return true;
    return false;
}

static HWND mkx(DWORD exstyle, DWORD style, HWND parent, INT x, INT y, INT cx, INT cy)
{
    static bool reg;
    if (!reg) {
        WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rec), .lpszClassName = u"T" };
        rt_wndclass_register(&wc, true);
        reg = true;
    }
    RtWindowCreate a = { .exstyle = exstyle, .style = style, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = x, .y = y, .cx = cx, .cy = cy, .parent = parent, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

static HWND mk(DWORD style, HWND parent, INT x, INT y, INT cx, INT cy)
{
    return mkx(0, style, parent, x, y, cx, cy);
}

static bool at(HWND h, LONG left, LONG top, LONG right, LONG bottom)
{
    RECT r;
    return rt_window_get_rect(h, &r) && r.left == left && r.top == top && r.right == right && r.bottom == bottom;
}

TEST(user32, SetWindowPos)
{
    HWND h, other, child, tool;

    h = mk(WS_POPUP, NULL, 10, 20, 300, 200);
    T_REQUIRE(h != NULL);

    T_CASE("moves and resizes a window, seen by GetWindowRect");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetWindowPos, h, HWND_TOP, 40, 50, 120, 80, SWP_NOACTIVATE), TRUE);
    T_TRUE(at(h, 40, 50, 160, 130));
    T_EQ(T_LAST_ERROR(), 0x1234);      /* success leaves last error alone */

    T_CASE("SWP_NOMOVE keeps the position and SWP_NOSIZE the size");
    T_EQ(CALL(user32, SetWindowPos, h, HWND_TOP, 0, 0, 100, 70, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(at(h, 40, 50, 140, 120));
    T_EQ(CALL(user32, SetWindowPos, h, HWND_TOP, 7, 9, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(at(h, 7, 9, 107, 79));
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a resize sends WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_WINDOWPOSCHANGED and WM_SIZE, and no WM_MOVE");
    nseen = 0;
    T_EQ(CALL(user32, SetWindowPos, h, HWND_TOP, 0, 0, 250, 150, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(SAW(0x46, 0x83, 0x47, 5));
    T_TRUE(!got(WM_MOVE));
    T_TRUE(at(h, 7, 9, 257, 159));

    T_CASE("nothing changed gives TRUE with only WM_WINDOWPOSCHANGING sent");
    nseen = 0;
    T_EQ(CALL(user32, SetWindowPos, h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(SAW(WM_WINDOWPOSCHANGING));

    T_CASE("SWP_NOSENDCHANGING sends no WM_WINDOWPOSCHANGING");
    nseen = 0;
    T_EQ(CALL(user32, SetWindowPos, h, HWND_TOP, 30, 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING), TRUE);
    T_TRUE(!got(WM_WINDOWPOSCHANGING));
    T_TRUE(at(h, 30, 40, 280, 190));

    T_CASE("an invalid window returns FALSE with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowPos, NULL, HWND_TOP, 1, 1, 1, 1, SWP_NOACTIVATE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowPos, (HWND)0x40000FF0, HWND_TOP, 1, 1, 1, 1, SWP_NOACTIVATE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    other = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(other != NULL);
    rt_window_destroy(T_CTX(), other);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, SetWindowPos, other, HWND_TOP, 1, 1, 1, 1, SWP_NOACTIVATE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    /* ---- stops: each one a fatal report, proved in a forked child ---- */
    other = mk(WS_POPUP, NULL, 0, 0, 10, 10);
    T_REQUIRE(other != NULL);
    T_TRAPS(CALL(user32, SetWindowPos, RT_HWND_DESKTOP, HWND_TOP, 1, 1, 1, 1, SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    T_TRAPS(CALL(user32, SetWindowPos, other, HWND_TOP, 1, 1, 1, 1, 0x8000u | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    T_TRAPS(CALL(user32, SetWindowPos, other, HWND_TOP, 1, 1, 1, 1, SWP_SHOWWINDOW | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    T_TRAPS(CALL(user32, SetWindowPos, other, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    T_TRAPS(CALL(user32, SetWindowPos, other, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    T_TRAPS(CALL(user32, SetWindowPos, other, (HWND)0x40000FF0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");

    /* A shown window: HIDEWINDOW is a trap; activating another window is a trap too. */
    rt_window_show(h, SW_SHOW);      /* returns the previous visibility: FALSE */
    T_TRAPS(CALL(user32, SetWindowPos, h, HWND_TOP, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    T_TRAPS(CALL(user32, SetWindowPos, other, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER), "rt_window_set_pos: not implemented");

    child = mk(WS_CHILD, h, 1, 1, 20, 20);
    T_REQUIRE(child != NULL);
    T_TRAPS(CALL(user32, SetWindowPos, child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER), "rt_window_set_pos: not implemented");

    edit = 1;
    T_TRAPS(CALL(user32, SetWindowPos, other, HWND_TOP, 2, 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    edit = 2;
    T_TRAPS(CALL(user32, SetWindowPos, other, HWND_TOP, 2, 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
    edit = 0;

    /* A tool-window caption: rt_window_frame refuses it, and creation answers WM_NCCALCSIZE
     * without the default procedure, so the record exists. */
    bare = true;
    tool = mkx(WS_EX_TOOLWINDOW, WS_POPUP | WS_CAPTION, NULL, 0, 0, 50, 50);
    bare = false;
    T_REQUIRE(tool != NULL);
    T_TRAPS(CALL(user32, SetWindowPos, tool, HWND_TOP, 0, 0, 60, 60, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), "rt_window_set_pos: not implemented");
}
