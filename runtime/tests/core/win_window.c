/* The piece win-window (rt/window.h, core/win_window.c): creation, the first show and the
 * hide, SetWindowPos, destruction, DefWindowProc, sending.
 *
 * Where the expected values come from: docs/reviews/win-window-sequences.md (Microsoft's
 * pages and Wine's user32/tests/msg.c and win.c, read by a reviewer), cited as "S<n>" for a
 * sequence and "row <n>" for a statement; docs/reviews/contracts-check.md and
 * contracts-check-2.md as "review C1, D1, ...".  Frame sizes: rt_window_frame's comment
 * (review N4's algorithm with this desktop's caption height).  None comes from the game.
 */
#include "rt/test.h"
#include "fpmode.h"
#include "../../core/win_priv.h"
#include "rt/input.h"

/* The mouse messages user.h lacks (as in core/win_window.c; values from Microsoft's pages). */
#define WM_CONTEXTMENU      0x007B
#define WM_LBUTTONDBLCLK    0x0203
#define WM_RBUTTONDBLCLK    0x0206
#define WM_MBUTTONDBLCLK    0x0209
#define WM_MOUSEWHEEL       0x020A
#define WM_XBUTTONDOWN      0x020B
#define WM_XBUTTONUP        0x020C
#define WM_XBUTTONDBLCLK    0x020D
#define WM_MOUSEHWHEEL      0x020E
#define WM_CAPTURECHANGED   0x0215
#define WM_MOUSEHOVER       0x02A1
#define WM_MOUSELEAVE       0x02A3

#define ML(lo, hi)  ((LPARAM)(uint32_t)(((uint32_t)(lo) & 0xFFFFu) | (((uint32_t)(hi) & 0xFFFFu) << 16)))
#define LOG_MAX 48

typedef struct { HWND h; UINT msg; WPARAM wp; LPARAM lp; UINT flags; } Rec;
static Rec      logv[LOG_MAX];
static unsigned logn;
static bool     proc_unicode = true;                /* which DefWindowProc the procedure passes on to */
static LRESULT  nccreate_answer = 1, create_answer = 0;
static CREATESTRUCTW seen_cs;

/* A window procedure that records what it gets and passes everything on to the default one. */
GUEST_FN(rec_proc)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    if (logn < LOG_MAX) {
        logv[logn] = (Rec){ h, msg, wp, lp, 0 };
        if ((msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED) && lp) logv[logn].flags = ((const WINDOWPOS *)(uintptr_t)lp)->flags;
        logn++;
    }
    if (msg == WM_NCCREATE) seen_cs = *(const CREATESTRUCTW *)(uintptr_t)lp;
    tf_fp_enter_host();
    LRESULT r = rt_window_def_proc(ctx, h, msg, wp, lp, proc_unicode);
    tf_fp_enter_guest(ctx->mxcsr);
    if (msg == WM_NCCREATE) r = nccreate_answer;
    if (msg == WM_CREATE) r = create_answer;
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

static void reg(const WCHAR *name)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rec_proc), .hInstance = (HINSTANCE)0x140000000ull, .lpszClassName = name };
    T_TRUE(rt_wndclass_register(&wc, true) != 0);
}
static HWND make(const WCHAR *cls, const WCHAR *title, DWORD style, DWORD ex, INT x, INT y, INT cx, INT cy, HWND parent, INT_PTR id)
{
    RtWindowCreate a = { .exstyle = ex, .style = style, .cls = RT_TEXT_W(cls), .title = RT_TEXT_W(title), .x = x, .y = y, .cx = cx, .cy = cy,
                         .parent = parent, .menu = (HMENU)(uintptr_t)id, .instance = (HINSTANCE)0x140000000ull, .param = (LPVOID)0x1234, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}
/* The log holds exactly these messages, in order, each for `h` (0: any window). */
static bool log_is(HWND h, const UINT *msgs, unsigned n)
{
    if (logn != n) return false;
    for (unsigned i = 0; i < n; i++)
        if (logv[i].msg != msgs[i] || (h && logv[i].h != h)) return false;
    return true;
}
#define LOG_IS(h, ...)  log_is((h), (const UINT[]){ __VA_ARGS__ }, sizeof((const UINT[]){ __VA_ARGS__ }) / sizeof(UINT))

TEST(core, win_window_create)
{
    reg(u"Rec");
    rt_thread_set_last_error(0xDEAD);

    T_CASE("a hidden popup: WM_NCCREATE, WM_NCCALCSIZE, WM_CREATE, WM_SIZE, WM_MOVE");    /* S2, rows 1.1, 1.3 */
    logn = 0;
    const WCHAR *title = u"Hello";
    HWND h = make(u"Rec", title, WS_POPUP, 0, 100, 200, 150, 150, NULL, 0);
    T_REQUIRE(h != NULL);
    T_TRUE(LOG_IS(h, WM_NCCREATE, WM_NCCALCSIZE, WM_CREATE, WM_SIZE, WM_MOVE));
    T_EQ(logv[1].wp, 0);
    T_EQ(logv[3].wp, SIZE_RESTORED); T_EQ(logv[3].lp, ML(150, 150));
    T_EQ(logv[4].lp, ML(100, 200));
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    RECT r;
    T_TRUE(rt_window_get_rect(h, &r) && r.left == 100 && r.top == 200 && r.right == 250 && r.bottom == 350);   /* review V33 */
    T_EQ(rt_window_is_visible(h), FALSE);

    T_CASE("the CREATESTRUCT carries the caller's values; the style gains WS_CLIPSIBLINGS");   /* rows 1.8, 1.10 */
    T_EQ((uintptr_t)seen_cs.lpCreateParams, 0x1234);
    T_EQ((DWORD)seen_cs.style, WS_POPUP);
    T_TRUE(seen_cs.cx == 150 && seen_cs.cy == 150 && seen_cs.x == 100 && seen_cs.y == 200);
    T_TRUE(seen_cs.lpszName == title);
    T_EQ((DWORD)rt_window_get_long(h, GWL_STYLE, 4, true), WS_POPUP | WS_CLIPSIBLINGS);
    HWND e1 = make(u"Rec", NULL, WS_POPUP, WS_EX_WINDOWEDGE, 0, 0, 10, 10, NULL, 0);
    T_EQ((DWORD)rt_window_get_long(e1, GWL_EXSTYLE, 4, true), 0);
    HWND e2 = make(u"Rec", NULL, WS_POPUP | WS_DLGFRAME, 0, 0, 0, 10, 10, NULL, 0);
    T_EQ((DWORD)rt_window_get_long(e2, GWL_EXSTYLE, 4, true), WS_EX_WINDOWEDGE);
    T_EQ(seen_cs.dwExStyle, WS_EX_WINDOWEDGE);
    HWND e3 = make(u"Rec", NULL, WS_POPUP | WS_DLGFRAME, WS_EX_STATICEDGE, 0, 0, 10, 10, NULL, 0);
    T_EQ((DWORD)rt_window_get_long(e3, GWL_EXSTYLE, 4, true), WS_EX_STATICEDGE);

    T_CASE("the title is stored by the default WM_NCCREATE and read back through WM_GETTEXT");   /* row 5.2, review V26 */
    WCHAR buf[16] = { 0 };
    logn = 0;
    T_EQ(rt_window_get_text(T_CTX(), h, buf, 16, true), 5);
    T_TRUE(buf[0] == u'H' && buf[4] == u'o' && buf[5] == 0);
    T_TRUE(LOG_IS(h, WM_GETTEXT));

    T_CASE("the game's own window: a caption and fixed frame around 1920 x 1080");
    /* The style pair and the request for a 1920 x 1080 client area are the recorded start's
     * (docs/trace/window-calls.md); the rectangle is that client area grown by rt_window_frame. */
    logn = 0;
    HWND g = make(u"Rec", u"Titanfall 2", 0x84ca0000u, 0x00040300u, -5, -28, 1930, 1113, NULL, 0);
    T_REQUIRE(g != NULL);
    T_TRUE(LOG_IS(g, WM_NCCREATE, WM_NCCALCSIZE, WM_CREATE, WM_SIZE, WM_MOVE));
    T_EQ(logv[3].lp, ML(1920, 1080));
    T_EQ(logv[4].lp, ML(0, 0));
    UINT cw = 0, ch = 0;
    T_TRUE(rt_window_client_size(g, &cw, &ch) && cw == 1920 && ch == 1080);
    T_TRUE(rt_window_get_client_rect(g, &r) && r.left == 0 && r.top == 0 && r.right == 1920 && r.bottom == 1080);
    T_TRUE(!rt_window_client_size((HWND)0x40000FF0, &cw, &ch));
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "CreateWindowEx: top-level window") && T_LOGGED(RT_CH_WINDOW, "titled \"Titanfall 2\""));

    T_CASE("a child: its own five messages, then WM_PARENTNOTIFY to the parent; last among its siblings");   /* review C2, D2 */
    logn = 0;
    HWND c = make(u"Rec", NULL, WS_CHILD, 0, 3, 4, 20, 30, h, 7);
    T_REQUIRE(c != NULL);
    T_EQ(logn, 6);
    T_TRUE(logv[0].h == c && logv[4].h == c && logv[4].msg == WM_MOVE && logv[4].lp == ML(3, 4));
    T_TRUE(logv[5].h == h && logv[5].msg == WM_PARENTNOTIFY && logv[5].wp == (WM_CREATE | (7u << 16)) && logv[5].lp == (LPARAM)(uintptr_t)c);
    T_EQ((DWORD)rt_window_get_long(c, GWL_STYLE, 4, true), WS_CHILD);
    logn = 0;
    HWND c2 = make(u"Rec", NULL, WS_CHILD, WS_EX_NOPARENTNOTIFY, 0, 0, -10, -10, h, 8);
    T_EQ(logn, 5);
    T_TRUE(rt_window_get_rect(c2, &r) && r.right == r.left && r.bottom == r.top);          /* a negative size is 0: review D8 */
    T_EQ((uintptr_t)rt_window_get(c, GW_HWNDNEXT), (uintptr_t)c2);

    T_CASE("what Windows refuses: no class, a child with no parent, a parent that is no window");   /* reviews V22, C3, S9 */
    logn = 0;
    rt_thread_set_last_error(0xDEAD);
    T_TRUE(make(u"NoSuchClass", NULL, WS_POPUP, 0, 0, 0, 1, 1, NULL, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_CANNOT_FIND_WND_CLASS);
    T_TRUE(make(u"Rec", NULL, WS_CHILD, 0, 0, 0, 1, 1, NULL, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_TLW_WITH_WSCHILD);
    T_TRUE(make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 1, 1, (HWND)0x40000FF0, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(logn, 0);

    /* Not built or not settled: each stops and names its case. */
    T_TRAPS(make(u"Rec", NULL, 0, 0, 0, 0, 100, 100, NULL, 0), "WM_GETMINMAXINFO comes first");                    /* review C1, row 1.2 */
    T_TRAPS(make(u"Rec", NULL, WS_POPUP | WS_THICKFRAME, 0, 0, 0, 100, 100, NULL, 0), "WM_GETMINMAXINFO comes first");
    T_TRAPS(make(u"Rec", NULL, WS_POPUP | WS_VISIBLE, 0, 0, 0, 1, 1, NULL, 0), "WS_VISIBLE at creation");
    T_TRAPS(make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 1, 1, HWND_MESSAGE, 0), "a message-only window");
    T_TRAPS(make(u"button", NULL, WS_POPUP, 0, 0, 0, 1, 1, NULL, 0), "a predefined control class");
    T_TRAPS(make(u"Rec", NULL, WS_POPUP, 0, CW_USEDEFAULT, 0, 1, 1, NULL, 0), "CW_USEDEFAULT");
    T_TRAPS(make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 1, 1, NULL, 9), "a menu");
    nccreate_answer = 0;
    T_TRAPS(make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 1, 1, NULL, 0), "refused WM_NCCREATE");                           /* row 1.13 */
    nccreate_answer = 1; create_answer = -1;
    T_TRAPS(make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 1, 1, NULL, 0), "refused WM_CREATE");
    create_answer = 0;
    RtWindowCreate narrow = { .style = WS_POPUP, .cls = RT_TEXT_A("Rec"), .title = RT_TEXT_A("ab"), .cx = 1, .cy = 1, .unicode = false };
    T_TRAPS(rt_window_create(T_CTX(), &narrow), "registered with the W function and created with the A one");       /* row 1.9: converted on Windows */
}

TEST(core, win_window_show)
{
    reg(u"Rec");
    HWND h = make(u"Rec", u"t", WS_POPUP | WS_CAPTION, 0, 10, 20, 300, 200, NULL, 0);
    T_REQUIRE(h != NULL);

    T_CASE("the first show of a hidden popup");                 /* S6 (S5 steps 1 to 8), rows 2.1, 2.3, 2.6, 2.8 */
    logn = 0;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_window_show(h, SW_SHOWDEFAULT), FALSE);
    T_TRUE(LOG_IS(h, WM_SHOWWINDOW, WM_WINDOWPOSCHANGING, WM_ACTIVATEAPP, WM_NCACTIVATE, WM_ACTIVATE, WM_SETFOCUS, WM_WINDOWPOSCHANGED));
    T_TRUE(logv[0].wp == 1 && logv[0].lp == 0);
    T_EQ(logv[1].flags, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE);
    T_TRUE(logv[2].wp == 1 && logv[3].wp == 1 && logv[4].wp == WA_ACTIVE && logv[5].wp == 0);
    T_EQ(logv[6].flags, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_EQ(rt_window_is_visible(h), TRUE);
    T_TRUE(rt_window_active() == h && rt_window_focus() == h && rt_window_foreground() == h);
    T_TRUE(((DWORD)rt_window_get_long(h, GWL_STYLE, 4, true) & WS_VISIBLE) != 0);
    T_EQ(rt_window_is_iconic(h), FALSE);

    T_CASE("a show of a window that is shown sends nothing and returns nonzero");       /* S7, row 2.2 */
    logn = 0;
    T_TRUE(rt_window_show(h, SW_SHOW) != 0);
    T_TRUE(rt_window_show(h, SW_SHOWNORMAL) != 0);
    T_EQ(logn, 0);

    T_CASE("SW_HIDE of the shown, active window");               /* S8, row 2.11 */
    logn = 0;
    T_TRUE(rt_window_show(h, SW_HIDE) != 0);
    T_TRUE(LOG_IS(h, WM_SHOWWINDOW, WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_NCACTIVATE, WM_ACTIVATE, WM_ACTIVATEAPP, WM_KILLFOCUS));
    T_TRUE(logv[0].wp == 0 && logv[3].wp == 0 && logv[4].wp == WA_INACTIVE && logv[5].wp == 0);
    T_EQ(logv[1].flags, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE);
    T_EQ(logv[2].flags, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE);
    T_EQ(rt_window_is_visible(h), FALSE);
    T_TRUE(rt_window_active() == NULL && rt_window_focus() == NULL && rt_window_foreground() == NULL);

    T_CASE("SW_HIDE of a hidden window sends nothing and returns 0");                   /* S9, review V34 */
    logn = 0;
    T_EQ(rt_window_show(h, SW_HIDE), FALSE);
    T_EQ(logn, 0);
    T_EQ(rt_window_show((HWND)0x40000FF0, SW_SHOW), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    HWND kid = make(u"Rec", NULL, WS_CHILD, 0, 0, 0, 5, 5, h, 1);
    T_TRAPS(rt_window_show(h, 3 /* SW_MAXIMIZE */), "only SW_HIDE, SW_SHOWNORMAL, SW_SHOW and SW_SHOWDEFAULT are built");
    T_TRAPS(rt_window_show(kid, SW_SHOW), "a child or a message-only window");
}

TEST(core, win_window_pos)
{
    reg(u"Rec");
    HWND h = make(u"Rec", NULL, WS_POPUP, 0, 10, 20, 100, 100, NULL, 0);
    HWND b = make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 5, 5, NULL, 0);
    T_REQUIRE(h != NULL && b != NULL);
    RECT r;

    T_CASE("a resize: WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_WINDOWPOSCHANGED, WM_SIZE; no WM_MOVE");   /* S13, rows 3.1, 3.3, 3.6; review D7 */
    logn = 0;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_window_set_pos(T_CTX(), h, NULL, 0, 0, 300, 200, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(LOG_IS(h, WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_WINDOWPOSCHANGED, WM_SIZE));
    T_EQ(logv[0].flags, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    T_EQ(logv[1].wp, TRUE);
    T_EQ(logv[2].flags, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCLIENTMOVE | SWP_NOREDRAW);     /* hidden: SWP_NOREDRAW */
    T_TRUE(logv[3].wp == SIZE_RESTORED && logv[3].lp == ML(300, 200));
    T_TRUE(rt_window_get_rect(h, &r) && r.left == 10 && r.top == 20 && r.right == 310 && r.bottom == 220);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("a move: WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_MOVE; no WM_NCCALCSIZE, no WM_SIZE");   /* S14 */
    logn = 0;
    T_EQ(rt_window_set_pos(T_CTX(), h, NULL, 50, 60, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(LOG_IS(h, WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_MOVE));
    T_EQ(logv[1].flags, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCLIENTSIZE | SWP_NOREDRAW);
    T_EQ(logv[2].lp, ML(50, 60));
    T_TRUE(rt_window_get_rect(h, &r) && r.left == 50 && r.top == 60 && r.right == 350 && r.bottom == 260);

    T_CASE("a call that changes nothing sends WM_WINDOWPOSCHANGING only");               /* row 3.8 */
    logn = 0;
    T_EQ(rt_window_set_pos(T_CTX(), h, NULL, 50, 60, 300, 200, SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(LOG_IS(h, WM_WINDOWPOSCHANGING));

    T_CASE("a frame change of a hidden window: both client flags, no WM_MOVE, no WM_SIZE");   /* S16 */
    logn = 0;
    T_EQ(rt_window_set_pos(T_CTX(), h, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED), TRUE);
    T_TRUE(LOG_IS(h, WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_WINDOWPOSCHANGED));
    T_EQ(logv[2].flags, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOREDRAW | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE);

    T_CASE("the Z order: to the top, to the bottom, after a sibling");
    T_TRUE(rt_window_top(NULL) == b);                           /* the newest is first: review V32 */
    T_EQ(rt_window_set_pos(T_CTX(), h, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE), TRUE);
    T_TRUE(rt_window_top(NULL) == h && rt_window_get(h, GW_HWNDNEXT) == b);
    T_EQ(rt_window_set_pos(T_CTX(), h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE), TRUE);
    T_TRUE(rt_window_top(NULL) == b && rt_window_get(b, GW_HWNDNEXT) == h);
    T_EQ(rt_window_set_pos(T_CTX(), b, h, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE), TRUE);
    T_TRUE(rt_window_top(NULL) == h && rt_window_get(h, GW_HWNDNEXT) == b);

    T_CASE("without SWP_NOACTIVATE the window is activated, hidden as it is");           /* row 3.9 */
    logn = 0;
    T_EQ(rt_window_set_pos(T_CTX(), h, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER), TRUE);
    T_TRUE(LOG_IS(h, WM_WINDOWPOSCHANGING, WM_ACTIVATEAPP, WM_NCACTIVATE, WM_ACTIVATE, WM_SETFOCUS, WM_WINDOWPOSCHANGED));
    T_TRUE(rt_window_active() == h && rt_window_focus() == h);

    T_CASE("a handle that is no window");
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_window_set_pos(T_CTX(), (HWND)0x40000FF0, NULL, 0, 0, 1, 1, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_TRAPS(rt_window_set_pos(T_CTX(), h, NULL, 0, 0, 1, 1, SWP_SHOWWINDOW), "SWP_SHOWWINDOW");
    T_TRAPS(rt_window_set_pos(T_CTX(), h, HWND_TOPMOST, 0, 0, 1, 1, SWP_NOACTIVATE), "HWND_TOPMOST");
    T_TRAPS(rt_window_set_pos(T_CTX(), h, NULL, 0, 0, 1, 1, 0x10000), "bits Windows does not define");
    T_TRAPS(rt_window_set_pos(T_CTX(), b, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER), "becomes active while window");
}

TEST(core, win_window_destroy)
{
    reg(u"Rec");
    HWND p = make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 100, 100, NULL, 0);
    HWND c = make(u"Rec", NULL, WS_CHILD, 0, 0, 0, 50, 50, p, 5);
    HWND g = make(u"Rec", NULL, WS_CHILD, WS_EX_NOPARENTNOTIFY, 0, 0, 10, 10, c, 6);
    HWND o = make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 10, 10, p, 0);
    HWND c2 = make(u"Rec", NULL, WS_CHILD, 0, 0, 0, 5, 5, p, 9);
    T_REQUIRE(p && c && g && o && c2);

    T_CASE("a child destroyed alone tells its parent first");   /* review D1, row 4.7 */
    logn = 0;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_window_destroy(T_CTX(), c2), TRUE);
    T_EQ(logn, 3);
    T_TRUE(logv[0].h == p && logv[0].msg == WM_PARENTNOTIFY && logv[0].wp == (WM_DESTROY | (9u << 16)) && logv[0].lp == (LPARAM)(uintptr_t)c2);
    T_TRUE(logv[1].h == c2 && logv[1].msg == WM_DESTROY && logv[2].h == c2 && logv[2].msg == WM_NCDESTROY);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("the owned window first; WM_DESTROY parent before children; WM_NCDESTROY children before parent");   /* review D1 */
    logn = 0;
    T_EQ(rt_window_destroy(T_CTX(), p), TRUE);
    static const struct { int who; UINT msg; } want[] = { { 3, WM_DESTROY }, { 3, WM_NCDESTROY }, { 0, WM_DESTROY }, { 1, WM_DESTROY },
                                                          { 2, WM_DESTROY }, { 2, WM_NCDESTROY }, { 1, WM_NCDESTROY }, { 0, WM_NCDESTROY } };
    const HWND who[] = { p, c, g, o };
    T_EQ(logn, 8);
    for (unsigned i = 0; i < 8 && i < logn; i++) T_TRUE(logv[i].h == who[want[i].who] && logv[i].msg == want[i].msg);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_TRUE(!rt_window_get_rect(p, &(RECT){ 0 }) && !rt_window_get_rect(g, &(RECT){ 0 }) && !rt_window_get_rect(o, &(RECT){ 0 }));   /* row 4.4 */

    T_CASE("a shown, active window is hidden and deactivated before WM_DESTROY");        /* S18, rows 4.1, 4.2 */
    HWND s = make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 100, 100, NULL, 0);
    rt_window_show(s, SW_SHOW);
    T_TRUE(rt_window_active() == s);
    logn = 0;
    T_EQ(rt_window_destroy(T_CTX(), s), TRUE);
    T_TRUE(LOG_IS(s, WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_NCACTIVATE, WM_ACTIVATE, WM_ACTIVATEAPP, WM_KILLFOCUS, WM_DESTROY, WM_NCDESTROY));
    T_EQ(logv[0].flags, SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
    T_EQ(logv[1].flags, SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE);
    T_TRUE(rt_window_active() == NULL && rt_window_focus() == NULL);

    T_CASE("WM_CLOSE through the default procedure destroys the window");                /* review V24, row 5.8 */
    HWND k = make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 1, 1, NULL, 0);
    T_EQ(rt_window_def_proc(T_CTX(), k, WM_CLOSE, 0, 0, true), 0);
    T_TRUE(!rt_window_get_rect(k, &(RECT){ 0 }));

    T_CASE("a window of another thread, and a handle that is no window");                /* review V15, row 4.5 */
    rt_win_lock();
    RtClassKey key = { .name = u"Rec" };
    RtWndClass *cls = rt_win_class_lookup(&key);
    rt_win_unlock();
    RtWinNew other = { .cls = cls, .style = WS_POPUP, .rect = { 0, 0, 1, 1 }, .unicode = true, .thread_id = LD32(T_CTX()->gs_base + 0x48) + 4 };
    HWND x = rt_win_window_new(&other);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_window_destroy(T_CTX(), x), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_TRUE(rt_window_get_rect(x, &(RECT){ 0 }));
    T_EQ(rt_window_destroy(T_CTX(), (HWND)0x40000FF0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRAPS(rt_window_destroy(T_CTX(), RT_HWND_DESKTOP), "the desktop window");
}

GUEST_FN(answer_77) { ctx->r[0] = 77 + ctx->r[8]; GUEST_RET(ctx); }

TEST(core, win_window_proc)
{
    reg(u"Rec");
    HWND h = make(u"Rec", NULL, WS_POPUP, 0, 0, 0, 10, 10, NULL, 0);
    T_REQUIRE(h != NULL);
    WCHAR buf[8];

    T_CASE("the title through WM_SETTEXT, WM_GETTEXT and WM_GETTEXTLENGTH");             /* reviews V25, V26 */
    logn = 0;
    T_EQ(rt_window_set_text(T_CTX(), h, RT_TEXT_W(u"abc")), TRUE);
    T_TRUE(LOG_IS(h, WM_SETTEXT));
    T_EQ(rt_window_get_text(T_CTX(), h, buf, 8, true), 3);
    T_TRUE(buf[0] == u'a' && buf[2] == u'c' && buf[3] == 0);
    buf[0] = u'z'; buf[1] = u'z';
    T_EQ(rt_window_get_text(T_CTX(), h, buf, 2, true), 1);
    T_TRUE(buf[0] == u'a' && buf[1] == 0);
    logn = 0;
    buf[0] = u'q';
    T_EQ(rt_window_get_text(T_CTX(), h, buf, 0, true), 0);                               /* cap 0: nothing is sent */
    T_TRUE(logn == 0 && buf[0] == u'q');
    T_EQ(rt_window_send(T_CTX(), h, WM_GETTEXTLENGTH, 0, 0, true), 3);
    T_EQ(rt_window_set_text(T_CTX(), h, RT_TEXT_W(NULL)), TRUE);
    T_EQ(rt_window_send(T_CTX(), h, WM_GETTEXTLENGTH, 0, 0, true), 0);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_window_set_text(T_CTX(), (HWND)0x40000FF0, RT_TEXT_W(u"x")), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(rt_window_get_text(T_CTX(), (HWND)0x40000FF0, buf, 8, true), 0);

    T_CASE("sending, dispatching and calling a procedure");
    logn = 0;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_window_send(T_CTX(), h, WM_USER + 1, 5, 6, true), 0);
    T_TRUE(LOG_IS(h, WM_USER + 1) && logv[0].wp == 5 && logv[0].lp == 6);
    MSG m = { .hwnd = h, .message = WM_USER + 2, .wParam = 1, .lParam = 2 };
    T_EQ(rt_window_dispatch(T_CTX(), &m, true), 0);
    T_EQ(logn, 2);
    m.hwnd = NULL;
    T_EQ(rt_window_dispatch(T_CTX(), &m, true), 0);
    T_EQ(logn, 2);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_EQ(rt_window_send(T_CTX(), (HWND)0x40000FF0, WM_USER, 0, 0, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(rt_window_call_proc(T_CTX(), T_GUEST_ADDR(answer_77), h, WM_USER, 3, 0, false), 80);

    T_CASE("the default procedure's answers");                  /* rows 5.2, 5.3, 5.11; reviews S24, V23 */
    T_EQ(rt_window_def_proc(T_CTX(), (HWND)0x40000FF0, WM_USER, 0, 0, true), 0);
    T_EQ(rt_window_def_proc(T_CTX(), h, WM_NCACTIVATE, FALSE, 0, true), TRUE);
    T_EQ(rt_window_def_proc(T_CTX(), h, WM_NCCALCSIZE, 0, 0, true), 0);
    T_EQ(rt_window_def_proc(T_CTX(), h, 0xC123, 1, 2, true), 0);
    HWND f = make(u"Rec", NULL, WS_POPUP | WS_CAPTION, WS_EX_CLIENTEDGE, 0, 0, 100, 100, NULL, 0);
    RECT r = { 10, 10, 110, 110 };
    T_EQ(rt_window_def_proc(T_CTX(), f, WM_NCCALCSIZE, 0, (LPARAM)(uintptr_t)&r, true), 0);
    T_TRUE(r.left == 15 && r.top == 38 && r.right == 105 && r.bottom == 105);           /* rt_window_frame: {5, 28, 5, 5} */

    T_TRAPS(rt_window_def_proc(T_CTX(), h, 0x0084 /* WM_NCHITTEST */, 0, 0, true), "its default processing is not built");
    T_TRAPS(rt_window_def_proc(T_CTX(), h, WM_GETMINMAXINFO, 0, 0, true), "its default processing is not built");   /* row 5.1: not settled */
    T_TRAPS(rt_window_def_proc(T_CTX(), h, WM_SETTEXT, 0, (LPARAM)(uintptr_t)"x", false), "the other character type");
    T_TRAPS(rt_window_send(T_CTX(), h, WM_SETTEXT, 0, (LPARAM)(uintptr_t)"x", false), "carries text");              /* rows 6.5, 6.7 */
    T_TRAPS(rt_window_send(T_CTX(), HWND_BROADCAST, WM_USER, 0, 0, true), "HWND_BROADCAST");
    T_TRAPS(rt_window_call_proc(T_CTX(), 0, h, WM_USER, 0, 0, true), "a NULL procedure");
    m.hwnd = h; m.message = WM_TIMER; m.lParam = 0x140001000ll;
    T_TRAPS(rt_window_dispatch(T_CTX(), &m, true), "WM_TIMER with a callback");
}

TEST(core, win_window_mouse)
{
    reg(u"Rec");
    LRESULT r;

    T_CASE("WM_RBUTTONUP: the default sends WM_CONTEXTMENU to the window, lParam in screen coordinates");   /* WM_CONTEXTMENU page, Remarks; WM_RBUTTONUP page */
    HWND t = make(u"Rec", NULL, WS_POPUP, 0, 100, 200, 150, 150, NULL, 0);     /* client origin (100, 200) */
    T_REQUIRE(t != NULL);
    logn = 0;
    r = rt_window_def_proc(T_CTX(), t, WM_RBUTTONUP, 0, ML(10, 20), true);
    T_EQ(r, 0);
    T_TRUE(LOG_IS(t, WM_CONTEXTMENU));
    T_EQ(logv[0].wp, (WPARAM)(uintptr_t)t);
    T_EQ(logv[0].lp, ML(110, 220));
    HWND p = make(u"Rec", NULL, WS_POPUP, 0, 100, 200, 150, 150, NULL, 0);
    HWND c = make(u"Rec", NULL, WS_CHILD, 0, 10, 10, 50, 50, p, 1);           /* client origin (110, 210) */
    T_REQUIRE(c != NULL);
    logn = 0;
    T_EQ(rt_window_def_proc(T_CTX(), c, WM_RBUTTONUP, 0, ML(5, 6), true), 0);
    T_TRUE(logn == 2 && logv[0].h == c && logv[0].msg == WM_CONTEXTMENU && logv[0].wp == (WPARAM)(uintptr_t)c);
    T_TRUE(logv[0].lp == ML(115, 216) && logv[1].h == p && logv[1].msg == WM_CONTEXTMENU);   /* the child passes it up */
    T_EQ(logv[1].wp, (WPARAM)(uintptr_t)c);
    T_EQ(logv[1].lp, ML(115, 216));

    T_CASE("WM_CONTEXTMENU: a child passes it to its parent and returns the parent's answer; a top-level window: 0");   /* WM_CONTEXTMENU page, Remarks */
    logn = 0;
    T_EQ(rt_window_send(T_CTX(), c, WM_CONTEXTMENU, (WPARAM)(uintptr_t)c, ML(5, 6), true), 0);
    T_TRUE(logn == 2 && logv[0].h == c && logv[1].h == p && logv[1].msg == WM_CONTEXTMENU);
    T_EQ(logv[1].wp, (WPARAM)(uintptr_t)c);
    T_EQ(logv[1].lp, ML(5, 6));
    logn = 0;
    T_EQ(rt_window_def_proc(T_CTX(), t, WM_CONTEXTMENU, (WPARAM)(uintptr_t)t, ML(5, 6), true), 0);
    T_TRUE(logn == 0);

    T_CASE("WM_MOUSEWHEEL and WM_MOUSEHWHEEL: the default passes them to the parent; no parent: 0");   /* WM_MOUSEWHEEL page; WM_MOUSEHWHEEL page */
    logn = 0;
    T_EQ(rt_window_send(T_CTX(), c, WM_MOUSEWHEEL, (WPARAM)0x780000, ML(5, 6), true), 0);
    T_TRUE(logn == 2 && logv[1].h == p && logv[1].msg == WM_MOUSEWHEEL && logv[1].wp == 0x780000 && logv[1].lp == ML(5, 6));
    logn = 0;
    T_EQ(rt_window_send(T_CTX(), c, WM_MOUSEHWHEEL, (WPARAM)0x780000, ML(5, 6), true), 0);
    T_TRUE(logn == 2 && logv[1].h == p && logv[1].msg == WM_MOUSEHWHEEL);
    logn = 0;
    T_EQ(rt_window_def_proc(T_CTX(), t, WM_MOUSEWHEEL, 0x780000, ML(5, 6), true), 0);
    T_TRUE(logn == 0);
    T_EQ(rt_window_def_proc(T_CTX(), t, WM_MOUSEHWHEEL, 0x780000, ML(5, 6), true), 0);

    T_CASE("the messages whose pages say nothing of DefWindowProc: do nothing, return 0");   /* see the comment in mouse_def_proc */
    const UINT plain[] = { WM_RBUTTONDOWN, WM_RBUTTONDBLCLK, WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK,
                           WM_LBUTTONDBLCLK, WM_XBUTTONDOWN, WM_XBUTTONDBLCLK, WM_MOUSELEAVE, WM_MOUSEHOVER, WM_CAPTURECHANGED };
    for (unsigned i = 0; i < sizeof plain / sizeof plain[0]; i++) {
        logn = 0;
        T_EQ(rt_window_def_proc(T_CTX(), t, plain[i], 0x1, ML(5, 6), true), 0);
        T_TRUE(logn == 0);
    }

    T_CASE("WM_XBUTTONUP stays a stop: its default is WM_APPCOMMAND, whose command is not on the pages");   /* WM_APPCOMMAND page */
    T_TRAPS(rt_window_def_proc(T_CTX(), t, WM_XBUTTONUP, 0x20, ML(5, 6), true), "its default processing is not built");
}
