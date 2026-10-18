/* The input script's loader and the state with no script (rt/input.h).  With no script
 * nothing of the input source exists: the cases of the first part are the answers of before
 * this unit (rt/window.h: GetKeyState 0, DefWindowProc stops on an input message). */
#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/rawinput.h"
#include "rt/input.h"

/* learn.microsoft.com, WM_LBUTTONDBLCLK: the double-click messages (the header of the runtime has not got them) */
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDBLCLK 0x0209

static WNDPROC defproc;

/* The activation messages the script's window gets, in order (msg, wParam); the case of "active" checks them. */
typedef struct { UINT msg; WPARAM wp; } Act;
static Act acts[16];
static unsigned nacts;
enum { ANY = 0xFFFF };                                  /* a wParam the case does not check */

/* Keeps WM_SETCURSOR, as the game's procedure does; the rest goes to DefWindowProcW. */
GUEST_FN(script_proc)
{
    LRESULT r = 0;
    tf_fp_enter_host();
    const UINT m = (UINT)ctx->r[2];
    if ((m == WM_NCACTIVATE || m == WM_ACTIVATE || m == WM_ACTIVATEAPP || m == WM_KILLFOCUS || m == WM_SETFOCUS) && nacts < 16)
        acts[nacts++] = (Act){ .msg = m, .wp = (WPARAM)ctx->r[8] };
    if ((UINT)ctx->r[2] != WM_SETCURSOR)
        r = rt_window_call_proc(ctx, defproc, (HWND)(uintptr_t)ctx->r[1], (UINT)ctx->r[2], ctx->r[8], (LPARAM)ctx->r[9], true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

/* The mouse messages in the queue, in order, up to max; PeekMessage takes the rest. */
static unsigned drain_mouse(UINT *got, unsigned max)
{
    MSG m;
    unsigned n = 0;
    while (CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE))
        if (m.message >= 0x0200 && m.message <= 0x0209 && n < max) got[n++] = m.message;
    return n;
}

/* The messages of the section just fired: what is due now, then, after the longest offset (700 ms), the rest. */
static unsigned settle(UINT *got, unsigned max)
{
    const unsigned n = drain_mouse(got, max);
    CALL(kernel32, SleepEx, 700, FALSE);
    return n + drain_mouse(got + n, max - n);
}

/* The activation messages recorded since nacts was 0: the same as `want` (wp ANY is not checked). */
static bool same_acts(const Act *want, unsigned k)
{
    if (nacts != k) return false;
    for (unsigned i = 0; i < k; i++)
        if (acts[i].msg != want[i].msg || (want[i].wp != ANY && acts[i].wp != want[i].wp)) return false;
    return true;
}

static bool same_seq(const UINT *got, unsigned n, const UINT *want, unsigned k)
{
    if (n != k) return false;
    for (unsigned i = 0; i < k; i++) if (got[i] != want[i]) return false;
    return true;
}

TEST(core, input_script)
{
    T_CASE("with no script: no device, no count, every key up, and DefWindowProc stops on a mouse message as before");
    unsigned events = 9, delivered = 9, dropped = 9;
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(!rt_input_active() && events == 0 && delivered == 0 && dropped == 0);
    T_EQ(rt_raw_input_devices(NULL, 0), 0);
    T_EQ(CALL(user32, GetKeyState, 0x41), 0);
    rt_input_present(5);                                /* nothing listens */
    defproc = rt_test_import_slot(RT_ID_user32_DefWindowProcW);
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .style = 0x0008u /* CS_DBLCLKS */, .lpfnWndProc = defproc, .lpszClassName = u"ScriptTest" };
    T_TRUE(CALL(user32, RegisterClassExW, &wc) >= 0xC000);
    const HWND h = CALL(user32, CreateWindowExW, 0x00040300u, u"ScriptTest", u"t", 0x84ca0000u, -5, -28, 1930, 1113, NULL, NULL, NULL, NULL);
    T_REQUIRE(h != NULL);
    CALL(user32, ShowWindow, h, 10);
    CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(script_proc));
    T_TRAPS(CALL(user32, DefWindowProcW, h, WM_MOUSEMOVE, 0, 0), "its default processing is not built");
    T_TRAPS(CALL(user32, DefWindowProcW, h, WM_INPUT, 0, 0), "its default processing is not built");

    T_CASE("a line the loader does not understand is refused with its number");
    T_TRAPS(rt_input_script_text("t", "0 moveto 1 1\n"), "t:1: an event before the first `anchor` line");
    T_TRAPS(rt_input_script_text("t", "anchor start\n0 wheel 120 0\n"), "t:2: \"wheel\" is not `moveto X Y`");
    T_TRAPS(rt_input_script_text("t", "anchor start\n5 move 1 1\n4 move 1 1\n"), "t:3: time 4");
    T_TRAPS(rt_input_script_text("t", "anchor present 0\n"), "t:1: Present number 0");
    T_TRAPS(rt_input_script_text("t", "anchor start\n0 key down 0x141\n"), "t:2: a key is");
    T_TRAPS(rt_input_script_text("t", "anchor start\n0 key down 0xA1\n"), "t:2: virtual key 0xa1 names one side of Shift");     /* K39 */
    T_TRAPS(rt_input_script_text("t", "anchor start\n0 key up 0x60\n"), "t:2: virtual key 0x60 is not in this runtime's US table");  /* K38 */
    T_TRAPS(rt_input_script_text("t", "# nothing\n"), "no `anchor` line");
    T_TRAPS(rt_input_script_load("/nonexistent/input.script"), "cannot read /nonexistent/input.script");

    T_CASE("with a script and no registration: the legacy messages alone; no window under the pointer: dropped and counted");
    rt_input_script_text("t",
        "anchor start\n"
        "0 moveto 100 100\n"
        "0 button left down\n"
        "0 button left up\n"
        "anchor present 1\n"
        "0 button left down\n"
        "anchor present +2\n"
        "0 button right down\n"
        "anchor present 53\n"                  /* right: down, up, double-click, up, then a plain down and up */
        "0 button right down\n"
        "100 button right up\n"
        "200 button right down\n"
        "300 button right up\n"
        "400 button right down\n"
        "500 button right up\n"
        "anchor present 54\n"                  /* middle: four quick presses are down, double-click, down, double-click */
        "0 button middle down\n"
        "50 button middle up\n"
        "100 button middle down\n"
        "150 button middle up\n"
        "200 button middle down\n"
        "250 button middle up\n"
        "300 button middle down\n"
        "350 button middle up\n"
        "anchor present 55\n"                  /* left: a press 3 px away after a press at the same place is plain */
        "0 moveto 400 400\n"
        "0 button left down\n"
        "50 button left up\n"
        "100 moveto 403 400\n"
        "150 button left down\n"
        "200 button left up\n"
        "anchor present 56\n"                  /* left: a second press 501 ms after the first is plain */
        "0 moveto 300 300\n"
        "0 button left down\n"
        "100 button left up\n"
        "501 button left down\n"
        "600 button left up\n"
        "anchor present 57\n"                  /* a class without CS_DBLCLKS: two downs, the window is the second one */
        "0 moveto 500 500\n"
        "0 button left down\n"
        "100 button left up\n"
        "200 button left down\n"
        "300 button left up\n"
        "anchor present 58\n"                  /* a point on the title bar of the framed window: nothing, dropped */
        "0 moveto 150 110\n"
        "0 button left down\n"
        "100 button left up\n"
        "anchor present 59\n"                 /* the host window stops being the key window: its messages, then nothing for the rest */
        "0 active off\n"
        "anchor present 60\n"                 /* and is key again */
        "0 active on\n"
        "anchor present 61\n"                 /* off, then off again: one set of messages */
        "0 active off\n"
        "anchor present 62\n"
        "0 active off\n"
        "anchor present 63\n"
        "0 active on\n"
        "anchor present 64\n"                 /* off and on due at the same time: no messages */
        "0 active off\n"
        "0 active on\n");
    MSG m;
    UINT got[8];
    unsigned n = 0;
    while (n < 8 && CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE)) got[n++] = m.message;
    T_TRUE(n == 3 && got[0] == WM_MOUSEMOVE && got[1] == WM_LBUTTONDOWN && got[2] == WM_LBUTTONUP);
    T_EQ(rt_raw_input_devices(NULL, 0), 2);
    /* the default procedure answers the hit test for the client area (C25) and 0 for the rest (C30) */
    T_EQ(CALL(user32, DefWindowProcW, h, WM_NCHITTEST, 0, (LPARAM)(100 << 16 | 100)), HTCLIENT);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_LBUTTONDOWN, 1, 0), 0);
    T_EQ(CALL(user32, DefWindowProcW, h, WM_RBUTTONUP, 0, 0), 0);      /* it sends WM_CONTEXTMENU (tests/core/win_window.c has the message) */
    T_TRAPS(CALL(user32, DefWindowProcW, h, WM_KEYDOWN, 0x79, 0), "its default processing is not built");

    T_CASE("a second press at the same place within the double-click time on a CS_DBLCLKS class is the double-click message");
    rt_input_present(1);
    T_TRUE(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE) && m.message == WM_LBUTTONDBLCLK);

    T_CASE("present +N counts from the anchor before it; a message for a hidden window is dropped and counted");
    rt_input_present(2);                                /* section 3 is `present +2` after the anchor of section 2 (Present 1): it waits for Present 3 */
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(events == 45 && delivered == 4 && dropped == 0);     /* 38 in sections 1 to 58, 7 in the active sections 59 to 64 */
    CALL(user32, ShowWindow, h, 0);
    rt_input_present(3);                                /* section 3 fires at Present 3; its press finds no shown window */
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(delivered == 5 && dropped == 1);
    rt_input_present(51);
    rt_input_counts(&events, &delivered, &dropped);
    T_EQ(delivered, 5);
    rt_input_present(52);
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(delivered == 5 && dropped == 1);

    T_CASE("a double click on a CS_DBLCLKS class for each button: down, up, double-click, up; the press after it is a plain down");
    CALL(user32, ShowWindow, h, 10);
    rt_input_present(53);
    static const UINT right[] = { WM_RBUTTONDOWN, WM_RBUTTONUP, WM_RBUTTONDBLCLK, WM_RBUTTONUP, WM_RBUTTONDOWN, WM_RBUTTONUP };
    UINT got2[16];
    unsigned k = settle(got2, 16);
    T_TRUE(same_seq(got2, k, right, 6));
    rt_input_present(54);
    static const UINT middle[] = { WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK, WM_MBUTTONUP,     /* the count starts over */
                                   WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK, WM_MBUTTONUP };
    k = settle(got2, 16);
    T_TRUE(same_seq(got2, k, middle, 8));

    T_CASE("a second press 3 px away is a plain down; a second press 501 ms after the first is a plain down");
    static const UINT three[] = { WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP };
    rt_input_present(55);
    k = settle(got2, 16);
    T_TRUE(same_seq(got2, k, three, 6));
    static const UINT ms501[] = { WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDOWN, WM_LBUTTONUP };
    rt_input_present(56);
    k = settle(got2, 16);
    T_TRUE(same_seq(got2, k, ms501, 5));

    T_CASE("a class without CS_DBLCLKS: two downs at the same place");
    CALL(user32, ShowWindow, h, 0);
    WNDCLASSEXW wc2 = { .cbSize = sizeof wc2, .style = 0, .lpfnWndProc = defproc, .lpszClassName = u"ScriptPlain" };
    T_TRUE(CALL(user32, RegisterClassExW, &wc2) >= 0xC000);
    const HWND h2 = CALL(user32, CreateWindowExW, 0u, u"ScriptPlain", u"p", 0x80000000u, 400, 400, 400, 300, NULL, NULL, NULL, NULL);
    T_REQUIRE(h2 != NULL);
    CALL(user32, ShowWindow, h2, 10);
    CALL(user32, SetWindowLongPtrW, h2, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(script_proc));
    static const UINT plain[] = { WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDOWN, WM_LBUTTONUP };
    rt_input_present(57);
    k = settle(got2, 16);
    T_TRUE(same_seq(got2, k, plain, 5));

    T_CASE("a press on the title bar of a framed window: no message, counted as dropped, no stop");
    CALL(user32, ShowWindow, h2, 0);
    CALL(user32, ShowWindow, h, 0);
    const HWND h3 = CALL(user32, CreateWindowExW, 0x00040300u, u"ScriptTest", u"t", 0x84ca0000u, 100, 100, 800, 600, NULL, NULL, NULL, NULL);
    T_REQUIRE(h3 != NULL);                              /* on the screen: its title bar is at y 100..128 */
    CALL(user32, ShowWindow, h3, 10);
    CALL(user32, SetWindowLongPtrW, h3, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(script_proc));
    unsigned events_before = 0, delivered_before = 0, dropped_before = 0;
    rt_input_counts(&events_before, &delivered_before, &dropped_before);
    rt_input_present(58);
    k = settle(got2, 16);
    unsigned events_after = 0, delivered_after = 0, dropped_after = 0;
    rt_input_counts(&events_after, &delivered_after, &dropped_after);
    T_TRUE(k == 0 && dropped_after == dropped_before + 3);

    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);

    T_CASE("active off from the script: after the thread pumps, WM_NCACTIVATE 0, WM_ACTIVATE 0, WM_ACTIVATEAPP 0, WM_KILLFOCUS; nothing is active, foreground or focused");
    T_TRUE(CALL(user32, GetActiveWindow) == h3 && CALL(user32, GetForegroundWindow) == h3 && CALL(user32, GetFocus) == h3);
    nacts = 0;
    rt_input_present(59);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    static const Act off_set[] = { { WM_NCACTIVATE, 0 }, { WM_ACTIVATE, 0 }, { WM_ACTIVATEAPP, 0 }, { WM_KILLFOCUS, ANY } };
    T_TRUE(same_acts(off_set, 4));
    T_TRUE(CALL(user32, GetActiveWindow) == NULL && CALL(user32, GetForegroundWindow) == NULL && CALL(user32, GetFocus) == NULL);

    T_CASE("active on from the script: WM_ACTIVATEAPP 1, WM_NCACTIVATE 1, WM_ACTIVATE 1, WM_SETFOCUS; the window answers again");
    nacts = 0;
    rt_input_present(60);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    static const Act on_set[] = { { WM_ACTIVATEAPP, 1 }, { WM_NCACTIVATE, 1 }, { WM_ACTIVATE, 1 }, { WM_SETFOCUS, ANY } };
    T_TRUE(same_acts(on_set, 4));
    T_TRUE(CALL(user32, GetActiveWindow) == h3 && CALL(user32, GetForegroundWindow) == h3 && CALL(user32, GetFocus) == h3);

    T_CASE("active off twice: one set of messages; the second off sends nothing");
    nacts = 0;
    rt_input_present(61);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_TRUE(same_acts(off_set, 4));
    nacts = 0;
    rt_input_present(62);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_TRUE(same_acts(NULL, 0));

    T_CASE("active on again, then off and on due at the same time: that thread sees the host key, so nothing is sent");
    nacts = 0;
    rt_input_present(63);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_TRUE(same_acts(on_set, 4));
    nacts = 0;
    rt_input_present(64);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_TRUE(same_acts(NULL, 0));
    T_TRUE(CALL(user32, GetActiveWindow) == h3 && CALL(user32, GetFocus) == h3);
}
