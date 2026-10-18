/* The scripted input source end to end (rt/input.h): a test guest whose window procedure does
 * what the game's chain does (docs/trace/input.md, section 3: it reads the packet of every
 * WM_INPUT and the message time, keeps WM_SETCURSOR, passes the rest to DefWindowProcW)
 * receives a scripted movement, click and key through WM_INPUT and the legacy messages.
 *
 * Expected values come from docs/handoff/rt-contract-input.md, row by row ("C22"): Microsoft's
 * pages for the packets, the wParam and lParam of each message and GetKeyState; the order of
 * one event's messages from the recording of the real game (C24, C33).  The points and keys
 * are made up here; none is the recording's.
 */
#include <stddef.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/rawinput.h"
#include "rt/input.h"

typedef struct { UINT msg; WPARAM wp; LPARAM lp; LONG time; UINT got; RAWINPUT raw; } Seen;
static Seen     seen[64];
static unsigned nseen;
static WNDPROC  defproc;

GUEST_FN(game_proc)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    LRESULT r = 0;
    tf_fp_enter_host();
    Seen *s = &seen[nseen < 63 ? nseen++ : 63];
    *s = (Seen){ .msg = msg, .wp = wp, .lp = lp, .time = rt_msg_time() };
    if (msg == WM_INPUT) {
        UINT size = sizeof s->raw;
        s->got = rt_raw_input_data((HRAWINPUT)(uintptr_t)lp, RID_INPUT, &s->raw, &size);
    }
    if (msg != WM_SETCURSOR) r = rt_window_call_proc(ctx, defproc, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

/* Take and dispatch everything that waits; `until` messages must have reached the procedure
 * within two seconds. */
static void pump(unsigned until)
{
    MSG m;
    for (int i = 0; i < 2000 && nseen < until; i++) {
        while (CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE)) CALL(user32, DispatchMessageW, &m);
        if (nseen < until) usleep(1000);
    }
}

static bool order(const UINT *want, unsigned n)
{
    if (nseen != n) return false;
    for (unsigned i = 0; i < n; i++) if (seen[i].msg != want[i]) return false;
    return true;
}
#define ORDER(...)  order((const UINT[]){ __VA_ARGS__ }, sizeof((const UINT[]){ __VA_ARGS__ }) / sizeof(UINT))
#define AT(x, y)    ((LPARAM)(((uint32_t)(y) << 16) | (uint32_t)(x)))

TEST(core, input_source)
{
    T_CASE("the packets' sizes and offsets on a 64-bit Windows (C1 to C5)");
    T_EQ(sizeof(RAWINPUTHEADER), 24);
    T_EQ(sizeof(RAWMOUSE), 24);
    T_EQ(sizeof(RAWKEYBOARD), 16);
    T_EQ(sizeof(RAWINPUT), 48);
    T_TRUE(offsetof(RAWINPUTHEADER, hDevice) == 8 && offsetof(RAWINPUTHEADER, wParam) == 16 && offsetof(RAWINPUT, data) == 24);
    T_TRUE(offsetof(RAWMOUSE, usButtonFlags) == 4 && offsetof(RAWMOUSE, usButtonData) == 6 && offsetof(RAWMOUSE, ulRawButtons) == 8
           && offsetof(RAWMOUSE, lLastX) == 12 && offsetof(RAWMOUSE, lLastY) == 16 && offsetof(RAWMOUSE, ulExtraInformation) == 20);
    T_TRUE(offsetof(RAWKEYBOARD, VKey) == 6 && offsetof(RAWKEYBOARD, Message) == 8 && offsetof(RAWKEYBOARD, ExtraInformation) == 12);

    T_CASE("a window with the game's styles, its procedure, the registration: the mouse as the game registers it, and the keyboard");
    defproc = rt_test_import_slot(RT_ID_user32_DefWindowProcW);
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = defproc, .lpszClassName = u"InputTest" };
    T_TRUE(CALL(user32, RegisterClassExW, &wc) >= 0xC000);
    const HWND h = CALL(user32, CreateWindowExW, 0x00040300u, u"InputTest", u"t", 0x84ca0000u, -5, -28, 1930, 1113, NULL, NULL, NULL, NULL);
    T_REQUIRE(h != NULL);
    CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(game_proc));
    CALL(user32, ShowWindow, h, 10);
    RECT rc;
    T_TRUE(CALL(user32, GetClientRect, h, &rc) && rc.right == 1920 && rc.bottom == 1080);
    RAWINPUTDEVICE reg[2] = { { 1, 2, RIDEV_INPUTSINK, h }, { 1, 6, 0, h } };
    T_EQ(CALL(user32, RegisterRawInputDevices, reg, 2, 16), TRUE);
    nseen = 0;

    T_CASE("the script is loaded: nothing is delivered before its time, and the counts say so");
    rt_input_script_text("test",
        "# hand-written\n"
        "anchor start\n"
        "0 moveto 700 400\n"
        "0 button left down\n"
        "0 button left up\n"
        "0 key down 0x41 0x1e\n"
        "0 key up 0x41 0x1e\n"
        "anchor present 2\n"
        "0 move 10 0\n"
        "0 moveto 300 200\n"
        "anchor cursor-shown\n"
        "0 key down 0x57 0x11\n"
        "300 key down 0x57 0x11\n"
        "600 key up 0x57 0x11\n"
        "anchor present 100\n"
        "0 moveto 301 200\n"
        "anchor present 200\n"
        "0 key down 0x12 0x38\n");
    unsigned events = 0, delivered = 9, dropped = 9;
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(events == 12 && delivered == 0 && dropped == 0 && rt_input_active());
    POINT pt;

    T_CASE("a movement, a click and a key: WM_INPUT first, then the hit test, WM_SETCURSOR and the legacy message");
    pump(16);
    T_TRUE(ORDER(WM_INPUT, WM_NCHITTEST, WM_SETCURSOR, WM_MOUSEMOVE, WM_INPUT, WM_NCHITTEST, WM_SETCURSOR, WM_LBUTTONDOWN,
                 WM_INPUT, WM_NCHITTEST, WM_SETCURSOR, WM_LBUTTONUP, WM_INPUT, WM_KEYDOWN, WM_INPUT, WM_KEYUP));
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(delivered == 5 && dropped == 0);
    /* the movement: from the start point (960, 540) to (700, 400) */
    T_TRUE(seen[0].wp == RIM_INPUT && seen[0].got == 48 && seen[0].raw.header.dwType == RIM_TYPEMOUSE && seen[0].raw.header.dwSize == 48
           && seen[0].raw.header.hDevice == RT_HRAWDEV_MOUSE && seen[0].raw.header.wParam == RIM_INPUT);
    T_TRUE(seen[0].raw.data.mouse.usFlags == 0 && seen[0].raw.data.mouse.usButtonFlags == 0 && seen[0].raw.data.mouse.lLastX == -260
           && seen[0].raw.data.mouse.lLastY == -140);
    T_TRUE(seen[1].wp == 0 && seen[1].lp == AT(700, 400));
    T_TRUE(seen[2].wp == (WPARAM)(uintptr_t)h && seen[2].lp == (LPARAM)(HTCLIENT | WM_MOUSEMOVE << 16));
    T_TRUE(seen[3].wp == 0 && seen[3].lp == AT(700, 400));
    /* the press and the release */
    T_TRUE(seen[4].raw.data.mouse.usButtonFlags == RI_MOUSE_LEFT_BUTTON_DOWN && seen[4].raw.data.mouse.lLastX == 0 && seen[4].raw.data.mouse.lLastY == 0);
    T_TRUE(seen[6].lp == (LPARAM)(HTCLIENT | WM_LBUTTONDOWN << 16) && seen[7].wp == MK_LBUTTON && seen[7].lp == AT(700, 400));
    T_TRUE(seen[8].raw.data.mouse.usButtonFlags == RI_MOUSE_LEFT_BUTTON_UP && seen[10].lp == (LPARAM)(HTCLIENT | WM_LBUTTONUP << 16));
    T_TRUE(seen[11].wp == 0 && seen[11].lp == AT(700, 400));
    /* the key: a keyboard packet of 40 bytes, then the message for the focus window */
    T_TRUE(seen[12].got == 40 && seen[12].raw.header.dwType == RIM_TYPEKEYBOARD && seen[12].raw.header.dwSize == 40
           && seen[12].raw.header.hDevice == RT_HRAWDEV_KEYBOARD);
    T_TRUE(seen[12].raw.data.keyboard.MakeCode == 0x1e && seen[12].raw.data.keyboard.Flags == RI_KEY_MAKE && seen[12].raw.data.keyboard.Reserved == 0
           && seen[12].raw.data.keyboard.VKey == 0x41 && seen[12].raw.data.keyboard.Message == WM_KEYDOWN);
    T_TRUE(seen[13].wp == 0x41 && seen[13].lp == 0x001e0001);
    T_TRUE(seen[14].raw.data.keyboard.Flags == RI_KEY_BREAK && seen[14].raw.data.keyboard.Message == WM_KEYUP);
    T_TRUE(seen[15].wp == 0x41 && seen[15].lp == (LPARAM)0xc01e0001u);
    T_TRUE(seen[0].time == seen[3].time && seen[15].time == CALL(user32, GetMessageTime));

    T_CASE("outside messages: the pointer is where the script put it; GetKeyState toggled once for the key and the button, both up");
    T_TRUE(CALL(user32, GetCursorPos, &pt) && pt.x == 700 && pt.y == 400);
    T_EQ(CALL(user32, GetKeyState, 0x41), 1);
    T_EQ(CALL(user32, GetKeyState, 1), 1);
    T_EQ(CALL(user32, GetKeyState, 0x42), 0);

    T_CASE("the present anchor: two movements are merged into one WM_MOUSEMOVE behind both WM_INPUT; a posted message comes first");
    nseen = 0;
    rt_input_present(1);
    pump(0);
    T_EQ(nseen, 0);
    rt_input_present(2);                                /* delivers: the queue now holds WM_INPUT, WM_INPUT, WM_MOUSEMOVE */
    T_EQ(CALL(user32, PostMessageA, h, WM_USER + 7, 1, 2), TRUE);
    pump(6);
    T_TRUE(ORDER(WM_USER + 7, WM_INPUT, WM_INPUT, WM_NCHITTEST, WM_SETCURSOR, WM_MOUSEMOVE));
    T_TRUE(seen[1].raw.data.mouse.lLastX == 10 && seen[1].raw.data.mouse.lLastY == 0);
    T_TRUE(seen[2].raw.data.mouse.lLastX == -410 && seen[2].raw.data.mouse.lLastY == -200);
    T_TRUE(seen[5].lp == AT(300, 200));

    T_CASE("the cursor anchor: a held key repeats with bit 30 set; GetKeyState is down while the thread has read the press");
    nseen = 0;
    T_EQ(CALL(user32, ShowCursor, FALSE), -1);
    pump(0);
    T_EQ(CALL(user32, ShowCursor, TRUE), 0);
    pump(2);
    T_TRUE(ORDER(WM_INPUT, WM_KEYDOWN) && seen[1].wp == 0x57 && seen[1].lp == 0x00110001);
    T_EQ((USHORT)CALL(user32, GetKeyState, 0x57), 0x8001);
    rt_input_counts(&events, &delivered, &dropped);
    T_EQ(delivered, 8);                                 /* the repeat is due 300 ms after the anchor, not before */
    pump(6);
    T_TRUE(ORDER(WM_INPUT, WM_KEYDOWN, WM_INPUT, WM_KEYDOWN, WM_INPUT, WM_KEYUP));
    T_TRUE(seen[3].lp == 0x40110001 && seen[5].lp == (LPARAM)0xc0110001u);
    T_TRUE(seen[3].time - seen[1].time == 300 && seen[5].time - seen[1].time == 600);
    T_EQ(CALL(user32, GetKeyState, 0x57), 1);
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(delivered == 10 && dropped == 0);

    T_CASE("the loud stops");
    MSG m;
    T_TRAPS(rt_raw_input_data((HRAWINPUT)(uintptr_t)seen[0].lp, RID_INPUT, &seen[0].raw, &seen[0].got), "whose packet is gone");
    T_TRAPS((rt_input_present(100), CALL(user32, PeekMessageW, &m, NULL, 0x200, 0x200, 0)), "a look without PM_REMOVE at a waiting mouse message");
    T_TRAPS((rt_input_present(200), pump(99)), "ALT, F10 and a key while ALT is down are system keys");
    T_TRAPS(rt_input_script_text("second", "anchor start\n"), "a second script");
}
