/* The input script's `anchor mark NAME` (rt/input.h; unit gate3-run-13, item 2): a section
 * that starts when the driver reports a mark with rt_input_mark, for a click that is anchored
 * on something the game does (its UI opening the main menu) instead of on a Present count.
 * The expected values are the header's statements: a mark is a moment, fires the section that
 * waits for it now, once, and one reported while the script waits for another anchor is not
 * kept. */
#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/input.h"

static WNDPROC defproc;

/* Keeps WM_SETCURSOR, as the game's procedure does; the rest goes to DefWindowProcW. */
GUEST_FN(mark_proc)
{
    LRESULT r = 0;
    tf_fp_enter_host();
    if ((UINT)ctx->r[2] != WM_SETCURSOR)
        r = rt_window_call_proc(ctx, defproc, (HWND)(uintptr_t)ctx->r[1], (UINT)ctx->r[2], ctx->r[8], (LPARAM)ctx->r[9], true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

static unsigned take(UINT *got, unsigned cap) {
    MSG m;
    unsigned n = 0;
    while (n < cap && CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE)) got[n++] = m.message;
    return n;
}

TEST(core, input_mark)
{
    unsigned events = 9, delivered = 9, dropped = 9;
    UINT got[8];

    T_CASE("a mark's name is a-z, 0-9 and `-`; anything else, and `mark` without a name, is refused with its line");
    T_TRAPS(rt_input_script_text("t", "anchor mark Main_Menu\n"), "t:1: mark \"Main_Menu\"");
    T_TRAPS(rt_input_script_text("t", "anchor start\nanchor mark\n"), "t:2: an anchor is `start`, `present N`, `present +N`, `cursor-shown` or `mark NAME`");
    T_TRAPS(rt_input_script_text("t", "anchor mark a-name-that-is-longer-than-a-mark-may-be\n"), "t:1: mark ");

    T_CASE("with no script a mark does nothing");
    rt_input_mark("main-menu");
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(!rt_input_active() && events == 0 && delivered == 0);

    defproc = rt_test_import_slot(RT_ID_user32_DefWindowProcW);
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = defproc, .lpszClassName = u"MarkTest" };
    T_TRUE(CALL(user32, RegisterClassExW, &wc) >= 0xC000);
    const HWND h = CALL(user32, CreateWindowExW, 0x00040300u, u"MarkTest", u"t", 0x84ca0000u, -5, -28, 1930, 1113, NULL, NULL, NULL, NULL);
    T_REQUIRE(h != NULL);
    CALL(user32, ShowWindow, h, 10);
    CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(mark_proc));

    T_CASE("a section waits for its mark: Presents, another mark's name and the mark of a later section do not fire it");
    rt_input_script_text("t",
        "anchor mark main-menu\n"
        "0 moveto 100 100\n"
        "0 button left down\n"
        "anchor mark later\n"
        "0 button left up\n"
        "anchor present +2\n"
        "0 button right down\n");
    rt_input_present(1000);
    rt_input_mark("other");
    rt_input_mark("later");                             /* not kept: the script waits for `main-menu` */
    rt_input_mark(NULL);
    T_EQ(take(got, 8), 0);
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(events == 4 && delivered == 0);
    T_TRUE(!T_LOGGED(RT_CH_WINDOW, "the anchor of section 1"));

    T_CASE("the mark fires it, at the Present the run is at; its events are then delivered as any others");
    rt_input_mark("main-menu");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "the anchor of section 1 (mark main-menu) fired at Present 1000"));
    unsigned n = take(got, 8);
    T_TRUE(n == 2 && got[0] == WM_MOUSEMOVE && got[1] == WM_LBUTTONDOWN);

    T_CASE("the mark a second time does nothing; `later`, reported before its section waited, was not kept");
    rt_input_mark("main-menu");
    T_EQ(take(got, 8), 0);
    rt_input_counts(&events, &delivered, &dropped);
    T_EQ(delivered, 2);
    T_TRUE(!T_LOGGED(RT_CH_WINDOW, "the anchor of section 2"));

    T_CASE("the next section's mark fires that one; `present +N` after it counts from the Present the mark came at");
    rt_input_present(1010);
    rt_input_mark("later");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "the anchor of section 2 (mark later) fired at Present 1010"));
    n = take(got, 8);
    T_TRUE(n == 1 && got[0] == WM_LBUTTONUP);
    rt_input_present(1011);
    T_EQ(take(got, 8), 0);
    rt_input_present(1012);
    n = take(got, 8);
    T_TRUE(n == 1 && got[0] == WM_RBUTTONDOWN);
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(events == 4 && delivered == 4 && dropped == 0);
}
