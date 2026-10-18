/* The recorded start's window calls, in its order, THROUGH THE SHIMS (docs/trace/window-calls.md
 * sections 2.2 to 2.6): a class whose procedure is DefWindowProcW itself, the window with the
 * game's two styles, the context calls, two subclasses on top of each other that pass on with
 * CallWindowProcW and CallWindowProcA, the show, the pump, and then a move and the destruction,
 * which the game does not do but the piece must.  This is the proof of the sequence while the
 * run of the game itself stops before its window.
 *
 * Expected values: the messages are those of rt/window.h (rt_window_create, rt_window_show,
 * rt_window_set_pos, rt_window_destroy: docs/reviews/win-window-sequences.md S2, S6, S14, S18);
 * the rectangles are a 1920 x 1080 client area and rt_window_frame's insets for the two
 * styles, {5, 28, 5, 5}.  The recording gives the calls and their order, never a result.
 */
#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/gdi.h"

static UINT     seen[48];
static unsigned nseen;
static WNDPROC  below_engine, below_input;

/* The engine's procedure in the recording keeps WM_ACTIVATEAPP and WM_SETFOCUS and passes
 * the rest on with CallWindowProcA; this one also records what it gets. */
GUEST_FN(engine_proc)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    LRESULT r = 0;
    if (nseen < 48) seen[nseen++] = msg;
    if (msg != WM_ACTIVATEAPP && msg != WM_SETFOCUS) {
        tf_fp_enter_host();
        r = rt_window_call_proc(ctx, below_engine, h, msg, wp, lp, false);
        tf_fp_enter_guest(ctx->mxcsr);
    }
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

/* The input system's procedure: everything on with CallWindowProcW. */
GUEST_FN(input_proc)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    tf_fp_enter_host();
    const LRESULT r = rt_window_call_proc(ctx, below_input, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

static bool saw(const UINT *want, unsigned n)
{
    if (nseen != n) return false;
    for (unsigned i = 0; i < n; i++) if (seen[i] != want[i]) return false;
    return true;
}
#define SAW(...)  saw((const UINT[]){ __VA_ARGS__ }, sizeof((const UINT[]){ __VA_ARGS__ }) / sizeof(UINT))

TEST(core, win_game_window)
{
    const WNDPROC defproc = rt_test_import_slot(RT_ID_user32_DefWindowProcW);
    RECT r = { 0, 0, 1920, 1080 };
    MSG m;

    T_CASE("the class and the window: the creation messages go to our own DefWindowProcW");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, UnregisterClassW, u"Respawn001", NULL), FALSE);
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = defproc, .lpszClassName = u"Respawn001" };
    T_TRUE(CALL(user32, RegisterClassExW, &wc) >= 0xC000);
    T_EQ(CALL(user32, AdjustWindowRectEx, &r, 0x84ca0000u, FALSE, 0x00040300u), TRUE);
    T_TRUE(r.left == -5 && r.top == -28 && r.right == 1925 && r.bottom == 1085);
    CALL(kernel32, SetLastError, 0x1234);
    const HWND h = CALL(user32, CreateWindowExW, 0x00040300u, u"Respawn001", u"Titanfall 2", 0x84ca0000u, r.left, r.top,
                        r.right - r.left, r.bottom - r.top, NULL, NULL, NULL, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    WCHAR title[16];
    T_EQ(rt_window_get_text(T_CTX(), h, title, 16, true), 11);     /* stored by the default WM_NCCREATE */
    T_TRUE(title[0] == u'T' && title[10] == u'2');

    T_CASE("the main thread's calls: the context, and the two subclasses");
    const HDC dc = CALL(user32, GetDC, h);
    T_TRUE(dc != NULL);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, 8), 1920);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, 10), 1080);
    T_EQ(CALL(gdi32, GetDeviceCaps, dc, 116), 60);
    T_EQ(CALL(user32, ReleaseDC, h, dc), 1);
    below_engine = (WNDPROC)CALL(user32, GetWindowLongPtrW, h, GWLP_WNDPROC);
    T_EQ(below_engine, defproc);
    T_EQ((uint64_t)CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(engine_proc)), defproc);
    below_input = (WNDPROC)CALL(user32, GetWindowLongPtrW, h, GWLP_WNDPROC);
    T_EQ(below_input, T_GUEST_ADDR(engine_proc));
    T_EQ((uint64_t)CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(input_proc)), T_GUEST_ADDR(engine_proc));

    T_CASE("ShowWindow(SW_SHOWDEFAULT): the chain gets the show and the activation");
    nseen = 0;
    T_EQ(CALL(user32, ShowWindow, h, 10), FALSE);
    T_TRUE(SAW(0x18, 0x46, 0x1C, 0x86, 0x06, 0x07, 0x47));
    T_EQ(CALL(user32, IsIconic, h), FALSE);
    T_EQ(CALL(user32, IsWindowVisible, h), TRUE);
    T_TRUE(CALL(user32, GetClientRect, h, &r) && r.left == 0 && r.top == 0 && r.right == 1920 && r.bottom == 1080);
    T_TRUE(CALL(user32, GetForegroundWindow) == h && CALL(user32, GetActiveWindow) == h && CALL(user32, GetFocus) == h);
    UINT cw = 0, ch = 0;
    T_TRUE(rt_window_client_size(h, &cw, &ch) && cw == 1920 && ch == 1080);      /* what the swap chain asks */
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the pump: empty at once; a posted message is dispatched to the chain");
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_EQ(CALL(user32, PostMessageA, h, WM_USER + 1, 1, 2), TRUE);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), TRUE);
    CALL(user32, TranslateMessage, &m);
    nseen = 0;
    T_EQ(CALL(user32, DispatchMessageW, &m), 0);
    T_TRUE(SAW(WM_USER + 1));

    T_CASE("a move");
    nseen = 0;
    T_EQ(CALL(user32, SetWindowPos, h, NULL, 10, 20, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), TRUE);
    T_TRUE(SAW(0x46, 0x47, 0x03));
    T_TRUE(CALL(user32, GetWindowRect, h, &r) && r.left == 10 && r.top == 20 && r.right == 1940 && r.bottom == 1133);

    T_CASE("the destruction of the shown, active window, and of its class");
    nseen = 0;
    T_EQ(CALL(user32, UnregisterClassW, u"Respawn001", NULL), FALSE);    /* it still has its window, as in the recording */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, DestroyWindow, h), TRUE);
    T_TRUE(SAW(0x46, 0x47, 0x86, 0x06, 0x1C, 0x08, 0x02, 0x82));
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(CALL(user32, GetForegroundWindow) == NULL);
    T_EQ(CALL(user32, IsWindowVisible, h), FALSE);
    T_EQ(CALL(user32, UnregisterClassW, u"Respawn001", NULL), TRUE);
}
