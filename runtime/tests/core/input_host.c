/* The host source (rt/input.h: rt_input_host_attach, rt_input_host_push): a test guest whose
 * pump and window procedure are shaped like the game's (docs/trace/input.md, section 3) gets
 * pushed keys and mouse events, from this thread and from a second host thread.
 *
 * Each TEST runs in its own process, so the attach (which cannot be undone) is the state of
 * this test alone.  The cases that need no host run first, before the attach.
 * Expected values come from docs/handoff/rt-contract-input.md (C27, C28, K1 to K3, K38, the
 * A to Z row of the scan table: A is 0x1E) and from the header's contract, never from a run.
 */
#include <pthread.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/input.h"

typedef struct { UINT msg; WPARAM wp; LPARAM lp; } Seen;
static Seen     seen[64];
static unsigned nseen;
static WNDPROC  defproc;

GUEST_FN(host_proc)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    LRESULT r = 0;
    tf_fp_enter_host();
    /* WM_NCHITTEST and WM_SETCURSOR are what the owner's take of a mouse message sends (C24): not recorded */
    if (msg != WM_NCHITTEST && msg != WM_SETCURSOR) seen[nseen < 63 ? nseen++ : 63] = (Seen){ .msg = msg, .wp = ctx->r[8], .lp = (LPARAM)ctx->r[9] };
    if (msg != WM_KEYDOWN && msg != WM_SETCURSOR) r = rt_window_call_proc(ctx, defproc, h, msg, ctx->r[8], (LPARAM)ctx->r[9], true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

/* The game's pump until `until` messages reached the procedure (two seconds at most).
 * `translate`: call TranslateMessage on each key message, as the game does. */
static void pump(unsigned until, bool translate)
{
    MSG m;
    for (int i = 0; i < 2000 && nseen < until; i++) {
        while (nseen < until && CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE)) {
            if (translate) CALL(user32, TranslateMessage, &m);
            CALL(user32, DispatchMessageW, &m);
        }
        if (nseen < until) usleep(1000);
    }
}

static void make_window(HWND *out)
{
    defproc = rt_test_import_slot(RT_ID_user32_DefWindowProcW);
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = defproc, .lpszClassName = u"HostTest" };
    T_TRUE(CALL(user32, RegisterClassExW, &wc) >= 0xC000);
    /* a plain popup, no frame: its client area starts at its own corner, (10, 10) on the screen */
    const HWND h = CALL(user32, CreateWindowExW, 0u, u"HostTest", u"t", 0x80000000u, 10, 10, 300, 300, NULL, NULL, NULL, NULL);
    T_REQUIRE(h != NULL);
    CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(host_proc));
    CALL(user32, ShowWindow, h, 10);
    *out = h;
}

static RtHostInput key(uint8_t down, int32_t vk, int32_t scan, uint8_t ext)
{
    return (RtHostInput){ .kind = RT_HI_KEY, .down = down, .ext = ext, .a = vk, .b = scan };
}

static RtHostInput mouse(uint8_t kind, uint8_t down, int32_t a, int32_t b)
{
    return (RtHostInput){ .kind = kind, .down = down, .a = a, .b = b };
}

/* The second host thread: three key downs, in order. */
static bool pushed_thread[3];
static void *push_three(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        RtHostInput e = key(1, 0x42 + i, 0, 0);     /* B, C, D */
        pushed_thread[i] = rt_input_host_push(&e);
    }
    return NULL;
}

TEST(core, input_host)
{
    T_CASE("with no host and no script: rt_input_active is false");
    T_TRUE(!rt_input_active());
    unsigned events = 0, delivered = 0, dropped = 0;
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(events == 0 && delivered == 0 && dropped == 0);

    T_CASE("a push before the attach returns false and is counted as dropped");
    const RtHostInput before = key(1, 0x41, 0, 0);
    T_TRUE(!rt_input_host_push(&before));
    rt_input_counts(&events, &delivered, &dropped);
    T_EQ(dropped, 1);

    T_CASE("with a host attached and no script: rt_input_active is true (and a second attach is harmless)");
    rt_input_host_attach();
    rt_input_host_attach();
    T_TRUE(rt_input_active());

    T_CASE("a pushed key down and up for A: WM_KEYDOWN, WM_CHAR 'a' after TranslateMessage, WM_KEYUP, with scan code 0x1E");
    HWND h = NULL;
    make_window(&h);
    nseen = 0;
    RtHostInput down = key(1, 0x41, 0, 0), up = key(0, 0x41, 0, 0);
    T_TRUE(rt_input_host_push(&down) && rt_input_host_push(&up));
    pump(3, true);
    T_EQ(nseen, 3);
    T_TRUE(seen[0].msg == WM_KEYDOWN && seen[0].wp == 0x41 && seen[0].lp == (LPARAM)0x001E0001);
    T_TRUE(seen[1].msg == WM_CHAR && seen[1].wp == 'a' && seen[1].lp == (LPARAM)0x001E0001);
    T_TRUE(seen[2].msg == WM_KEYUP && seen[2].wp == 0x41 && seen[2].lp == (LPARAM)0xC01E0001u);

    T_CASE("a pushed MOVETO inside the window, then left button down and up: the point in client coordinates");
    nseen = 0;
    RtHostInput to = mouse(RT_HI_MOVETO, 0, 100, 100), bd = mouse(RT_HI_BUTTON, 1, 0, 0), bu = mouse(RT_HI_BUTTON, 0, 0, 0);
    T_TRUE(rt_input_host_push(&to) && rt_input_host_push(&bd) && rt_input_host_push(&bu));
    pump(3, false);
    T_EQ(nseen, 3);
    /* the screen point (100, 100) less the client origin (10, 10) */
    T_TRUE(seen[0].msg == WM_MOUSEMOVE && seen[0].lp == (LPARAM)0x005A005A);
    T_TRUE(seen[1].msg == WM_LBUTTONDOWN && seen[1].lp == (LPARAM)0x005A005A);
    T_TRUE(seen[2].msg == WM_LBUTTONUP && seen[2].lp == (LPARAM)0x005A005A);

    T_CASE("three events pushed from a second host thread while this one waits in the pump arrive in the order pushed");
    nseen = 0;
    pthread_t t;
    T_REQUIRE(pthread_create(&t, NULL, push_three, NULL) == 0);
    pump(3, false);
    pthread_join(t, NULL);
    T_TRUE(pushed_thread[0] && pushed_thread[1] && pushed_thread[2]);
    T_EQ(nseen, 3);
    T_TRUE(seen[0].msg == WM_KEYDOWN && seen[0].wp == 0x42);
    T_TRUE(seen[1].msg == WM_KEYDOWN && seen[1].wp == 0x43);
    T_TRUE(seen[2].msg == WM_KEYDOWN && seen[2].wp == 0x44);

    T_CASE("RT_HI_ACTIVE pushed with down 0, then down 1: the host window loses and regains the activation with the same messages");
    T_TRUE(CALL(user32, GetActiveWindow) == h && CALL(user32, GetFocus) == h);
    nseen = 0;
    RtHostInput host_off = { .kind = RT_HI_ACTIVE, .down = 0 }, host_on = { .kind = RT_HI_ACTIVE, .down = 1 };
    T_TRUE(rt_input_host_push(&host_off));
    pump(4, false);
    T_EQ(nseen, 4);
    T_TRUE(seen[0].msg == WM_NCACTIVATE && seen[0].wp == 0);
    T_TRUE(seen[1].msg == WM_ACTIVATE && seen[1].wp == 0);
    T_TRUE(seen[2].msg == WM_ACTIVATEAPP && seen[2].wp == 0);
    T_TRUE(seen[3].msg == WM_KILLFOCUS);
    T_TRUE(CALL(user32, GetActiveWindow) == NULL && CALL(user32, GetForegroundWindow) == NULL && CALL(user32, GetFocus) == NULL);
    nseen = 0;
    T_TRUE(rt_input_host_push(&host_on));
    pump(4, false);
    T_EQ(nseen, 4);
    T_TRUE(seen[0].msg == WM_ACTIVATEAPP && seen[0].wp == 1);
    T_TRUE(seen[1].msg == WM_NCACTIVATE && seen[1].wp == 1);
    T_TRUE(seen[2].msg == WM_ACTIVATE && seen[2].wp == 1);
    T_TRUE(seen[3].msg == WM_SETFOCUS);
    T_TRUE(CALL(user32, GetActiveWindow) == h && CALL(user32, GetForegroundWindow) == h && CALL(user32, GetFocus) == h);

    T_CASE("5000 pushes with nobody polling: the first 4096 return true, the rest false, and dropped rose by 904");
    rt_input_counts(&events, &delivered, &dropped);
    const unsigned dropped_before = dropped;
    unsigned ok = 0;
    for (int i = 0; i < 5000; i++) {
        RtHostInput m = mouse(RT_HI_MOVE, 0, 1, 0);
        if (rt_input_host_push(&m)) ok++;
    }
    rt_input_counts(&events, &delivered, &dropped);
    T_EQ(ok, 4096);
    T_EQ(dropped - dropped_before, 904);
}
