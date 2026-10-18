/* Scripted keys end to end (rt/input.h, rt/window.h: rt_key_translate): a test guest whose pump
 * and window procedure are shaped like the game's (docs/trace/input.md, section 3: the pump
 * takes a message with PeekMessageW, calls TranslateMessage and DispatchMessageW; the
 * procedure keeps WM_KEYDOWN and passes WM_CHAR and WM_KEYUP to DefWindowProcW; raw input is
 * registered for the mouse only) receives scripted keys and reads back WM_KEYDOWN, the WM_CHAR
 * that TranslateMessage posted, and WM_KEYUP.
 *
 * Expected values come from docs/handoff/rt-contract-input.md, row by row ("K28"): Microsoft's
 * pages for the lParam bits, TranslateMessage's answers and the scan codes, Wine's conformance
 * test of ToUnicode for the Control combinations.  The keys are made up here; none is the
 * recording's.
 */
#include <unistd.h>

#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/rawinput.h"
#include "rt/input.h"

enum { S_SHIFT = 1, S_LSHIFT = 2, S_RSHIFT = 4, S_CTRL = 8, S_LCTRL = 16, S_RCTRL = 32, S_CAPS = 64 };
typedef struct { UINT msg; WPARAM wp; LPARAM lp; unsigned state; BOOL translated; } Seen;
static Seen     seen[96];
static unsigned nseen;
static WNDPROC  defproc;
static BOOL     last_translate;

static unsigned key_state(void)
{
    return (rt_key_state(0x10) < 0 ? S_SHIFT : 0u) | (rt_key_state(0xA0) < 0 ? S_LSHIFT : 0u) | (rt_key_state(0xA1) < 0 ? S_RSHIFT : 0u)
         | (rt_key_state(0x11) < 0 ? S_CTRL : 0u) | (rt_key_state(0xA2) < 0 ? S_LCTRL : 0u) | (rt_key_state(0xA3) < 0 ? S_RCTRL : 0u)
         | (rt_key_state(0x14) & 1 ? S_CAPS : 0u);
}

GUEST_FN(key_proc)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    LRESULT r = 0;
    tf_fp_enter_host();
    seen[nseen < 95 ? nseen++ : 95] = (Seen){ .msg = msg, .wp = wp, .lp = lp, .state = key_state(), .translated = last_translate };
    if (msg != WM_KEYDOWN && msg != WM_SETCURSOR) r = rt_window_call_proc(ctx, defproc, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}

/* The game's pump, one message at a time, until `until` messages reached the procedure (two
 * seconds at most). */
static void pump(unsigned until)
{
    MSG m;
    for (int i = 0; i < 2000 && nseen < until; i++) {
        while (nseen < until && CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE)) {
            last_translate = CALL(user32, TranslateMessage, &m);
            CALL(user32, DispatchMessageW, &m);
        }
        if (nseen < until) usleep(1000);
    }
}

/* One message the procedure saw: the message, wParam, lParam and what TranslateMessage answered. */
static bool is(unsigned i, UINT msg, WPARAM wp, uint32_t lp, BOOL translated)
{
    return i < nseen && seen[i].msg == msg && seen[i].wp == wp && seen[i].lp == (LPARAM)lp && seen[i].translated == translated;
}
/* A key that is pressed and released with a character between: three messages from `i`. */
static bool typed(unsigned i, WPARAM vk, uint32_t scan, WPARAM ch)
{
    return is(i, WM_KEYDOWN, vk, scan << 16 | 1, TRUE) && is(i + 1, WM_CHAR, ch, scan << 16 | 1, FALSE)
        && is(i + 2, WM_KEYUP, vk, 0xC0000001u | scan << 16, TRUE);
}
/* ... with no character: two messages. */
static bool silent(unsigned i, WPARAM vk, uint32_t scan_and_ext)
{
    return is(i, WM_KEYDOWN, vk, scan_and_ext << 16 | 1, TRUE) && is(i + 1, WM_KEYUP, vk, 0xC0000001u | scan_and_ext << 16, TRUE);
}

static MSG key_msg(HWND h, UINT msg, WPARAM vk, uint32_t scan)
{
    return (MSG){ .hwnd = h, .message = msg, .wParam = vk, .lParam = (LPARAM)(scan << 16 | 1) };
}

TEST(core, input_keys)
{
    T_CASE("a window with the game's styles and its procedure; raw input for the mouse only, as the game registers it");
    defproc = rt_test_import_slot(RT_ID_user32_DefWindowProcW);
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = defproc, .lpszClassName = u"KeyTest" };
    T_TRUE(CALL(user32, RegisterClassExW, &wc) >= 0xC000);
    const HWND h = CALL(user32, CreateWindowExW, 0x00040300u, u"KeyTest", u"t", 0x84ca0000u, -5, -28, 1930, 1113, NULL, NULL, NULL, NULL);
    T_REQUIRE(h != NULL);
    CALL(user32, SetWindowLongPtrW, h, GWLP_WNDPROC, (LONG_PTR)T_GUEST_ADDR(key_proc));
    CALL(user32, ShowWindow, h, 10);
    RAWINPUTDEVICE reg = { 1, 2, RIDEV_INPUTSINK, h };
    T_EQ(CALL(user32, RegisterRawInputDevices, &reg, 1, 16), TRUE);
    nseen = 0;
    rt_input_script_text("keys",
        "anchor start\n"                /* a letter, held: the scan code is the table's (K38) */
        "0 key down 0x57\n" "0 key down 0x57\n" "0 key up 0x57\n"
        "anchor present 1\n"            /* Shift, left then right */
        "0 key down 0x10\n" "0 key down 0x57\n" "0 key up 0x57\n" "0 key down 0x31\n" "0 key up 0x31\n" "0 key down 0xBF\n" "0 key up 0xBF\n"
        "0 key up 0x10\n" "0 key down 0x10 0x36\n" "0 key down 0x41\n" "0 key up 0x41\n" "0 key up 0x10 0x36\n"
        "anchor present 2\n"            /* Caps Lock */
        "0 key down 0x14\n" "0 key up 0x14\n" "0 key down 0x51\n" "0 key up 0x51\n" "0 key down 0x32\n" "0 key up 0x32\n"
        "0 key down 0x10\n" "0 key down 0x51\n" "0 key up 0x51\n" "0 key up 0x10\n"
        "0 key down 0x14\n" "0 key up 0x14\n" "0 key down 0x51\n" "0 key up 0x51\n"
        "anchor present 3\n"            /* Control, left then right */
        "0 key down 0x11\n" "0 key down 0x57\n" "0 key up 0x57\n" "0 key down 0x34\n" "0 key up 0x34\n" "0 key down 0x0D\n" "0 key up 0x0D\n"
        "0 key down 0x20\n" "0 key up 0x20\n" "0 key down 0xDB\n" "0 key up 0xDB\n" "0 key down 0x09\n" "0 key up 0x09\n"
        "0 key down 0x10\n" "0 key down 0x32\n" "0 key up 0x32\n" "0 key down 0x36\n" "0 key up 0x36\n" "0 key down 0x46\n" "0 key up 0x46\n"
        "0 key up 0x10\n" "0 key up 0x11\n" "0 key down 0x11 0x1d ext\n" "0 key up 0x11 0x1d ext\n"
        "anchor present 4\n"            /* the keys with a control character, and two without a character */
        "0 key down 0x0D\n" "0 key up 0x0D\n" "0 key down 0x1B\n" "0 key up 0x1B\n" "0 key down 0x09\n" "0 key up 0x09\n"
        "0 key down 0x08\n" "0 key up 0x08\n" "0 key down 0x20\n" "0 key up 0x20\n"
        "0 key down 0x25\n" "0 key up 0x25\n" "0 key down 0x70\n" "0 key up 0x70\n"
        "anchor present 5\n"
        "0 key down 0x45\n" "0 key up 0x45\n"
        "anchor present 6\n"
        "0 key down 0x10\n"
        "anchor present 7\n"
        "0 key up 0x10\n" "0 key down 0x11\n");

    T_CASE("a held letter: WM_KEYDOWN, its WM_CHAR with the same lParam, the repeat with bit 30 and its WM_CHAR, WM_KEYUP (K2, K3, K5, K9)");
    pump(5);
    T_EQ(nseen, 5);
    T_TRUE(is(0, WM_KEYDOWN, 0x57, 0x00110001, TRUE) && is(1, WM_CHAR, 'w', 0x00110001, FALSE));
    T_TRUE(is(2, WM_KEYDOWN, 0x57, 0x40110001, TRUE) && is(3, WM_CHAR, 'w', 0x40110001, FALSE));
    T_TRUE(is(4, WM_KEYUP, 0x57, 0xC0110001u, TRUE));
    T_EQ(CALL(user32, GetKeyState, 0x57), 1);           /* up; toggled once, by the press and not by the repeat (K17) */
    MSG m;
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);     /* and no WM_INPUT came for a key (C12) */

    T_CASE("Shift: the capital, the upper legend of a digit and of a punctuation key; the state has the side (K18, K19, K24)");
    nseen = 0;
    rt_input_present(1);
    pump(16);
    T_EQ(nseen, 16);
    T_TRUE(is(0, WM_KEYDOWN, 0x10, 0x002A0001, TRUE) && seen[0].state == (S_SHIFT | S_LSHIFT));
    T_TRUE(typed(1, 0x57, 0x11, 'W') && typed(4, 0x31, 0x02, '!') && typed(7, 0xBF, 0x35, '?'));
    T_TRUE(is(10, WM_KEYUP, 0x10, 0xC02A0001u, TRUE) && seen[10].state == 0);
    T_TRUE(is(11, WM_KEYDOWN, 0x10, 0x00360001, TRUE) && seen[11].state == (S_SHIFT | S_RSHIFT));
    T_TRUE(typed(12, 0x41, 0x1E, 'A') && is(15, WM_KEYUP, 0x10, 0xC0360001u, TRUE) && seen[15].state == 0);

    T_CASE("Caps Lock: the capital while it is toggled, the small letter with Shift, a digit unchanged; off again (K14, K25)");
    nseen = 0;
    rt_input_present(2);
    pump(18);
    T_EQ(nseen, 18);
    T_TRUE(silent(0, 0x14, 0x3A) && seen[0].state == S_CAPS && seen[1].state == S_CAPS);
    T_TRUE(typed(2, 0x51, 0x10, 'Q') && typed(5, 0x32, 0x03, '2'));
    T_TRUE(is(8, WM_KEYDOWN, 0x10, 0x002A0001, TRUE) && typed(9, 0x51, 0x10, 'q') && is(12, WM_KEYUP, 0x10, 0xC02A0001u, TRUE));
    T_TRUE(silent(13, 0x14, 0x3A) && seen[13].state == 0 && typed(15, 0x51, 0x10, 'q'));

    T_CASE("Control: a letter's control character, nothing for a digit and Tab, 0x0A for Enter, Space, 0x1B for [; with Shift 0 for 2, "
           "0x1E for 6 (K28, K29)");
    nseen = 0;
    rt_input_present(3);
    pump(31);
    T_EQ(nseen, 31);
    T_TRUE(is(0, WM_KEYDOWN, 0x11, 0x001D0001, TRUE) && seen[0].state == (S_CTRL | S_LCTRL));
    T_TRUE(typed(1, 0x57, 0x11, 0x17) && silent(4, 0x34, 0x05) && typed(6, 0x0D, 0x1C, 0x0A) && typed(9, 0x20, 0x39, ' '));
    T_TRUE(typed(12, 0xDB, 0x1A, 0x1B) && silent(15, 0x09, 0x0F));
    T_TRUE(is(17, WM_KEYDOWN, 0x10, 0x002A0001, TRUE) && seen[17].state == (S_CTRL | S_LCTRL | S_SHIFT | S_LSHIFT));
    T_TRUE(typed(18, 0x32, 0x03, 0) && typed(21, 0x36, 0x07, 0x1E) && typed(24, 0x46, 0x21, 6));
    T_TRUE(is(27, WM_KEYUP, 0x10, 0xC02A0001u, TRUE) && is(28, WM_KEYUP, 0x11, 0xC01D0001u, TRUE) && seen[28].state == 0);
    T_TRUE(is(29, WM_KEYDOWN, 0x11, 0x011D0001, TRUE) && seen[29].state == (S_CTRL | S_RCTRL));
    T_TRUE(is(30, WM_KEYUP, 0x11, 0xC11D0001u, TRUE) && seen[30].state == 0);
    T_EQ(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE), FALSE);     /* nothing more: no character for 4 or Tab */

    T_CASE("Enter, Esc, Tab, Backspace and Space give their characters; an arrow (an extended key) and F1 give none (K23, K26, K32)");
    nseen = 0;
    rt_input_present(4);
    pump(19);
    T_EQ(nseen, 19);
    T_TRUE(typed(0, 0x0D, 0x1C, 0x0D) && typed(3, 0x1B, 0x01, 0x1B) && typed(6, 0x09, 0x0F, 0x09) && typed(9, 0x08, 0x0E, 0x08));
    T_TRUE(typed(12, 0x20, 0x39, ' ') && silent(15, 0x25, 0x14B) && silent(17, 0x70, 0x3B));

    T_CASE("the WM_CHAR is a post: behind a posted message that waits already, in front of the key-up that waits (K13)");
    nseen = 0;
    rt_input_present(5);
    T_TRUE(CALL(user32, PeekMessageW, &m, NULL, 0, 0, PM_REMOVE) && m.message == WM_KEYDOWN && m.hwnd == h);
    T_EQ(CALL(user32, PostMessageA, h, WM_USER + 7, 1, 2), TRUE);
    const MSG before = m;
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    T_EQ(memcmp(&m, &before, sizeof m), 0);             /* K10 */
    pump(3);
    T_TRUE(nseen == 3 && seen[0].msg == WM_USER + 7 && is(1, WM_CHAR, 'e', 0x00120001, FALSE) && is(2, WM_KEYUP, 0x45, 0xC0120001u, TRUE));

    T_CASE("the loud stops: system keys, a key outside the table, Shift+Enter, Control+Esc");
    m = key_msg(h, WM_SYSKEYDOWN, 0x41, 0x1E);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "a system key message 0x104");
    m = key_msg(h, WM_KEYDOWN, 0x60, 0x52);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "virtual key 0x60 is not in this runtime's US table");
    m = key_msg(h, WM_KEYDOWN, 0xE7, 0);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "VK_PACKET");
    nseen = 0;
    rt_input_present(6);
    pump(1);
    T_TRUE(nseen == 1 && seen[0].state == (S_SHIFT | S_LSHIFT));
    m = key_msg(h, WM_KEYDOWN, 0x0D, 0x1C);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "Shift+Enter");
    rt_input_present(7);
    pump(3);
    T_TRUE(nseen == 3 && seen[2].state == (S_CTRL | S_LCTRL));
    m = key_msg(h, WM_KEYDOWN, 0x1B, 0x01);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "Control+Esc");
    m = key_msg(h, WM_KEYDOWN, 0x08, 0x0E);             /* Control+Backspace: 0x7F (K30) */
    nseen = 0;
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    pump(1);
    T_TRUE(nseen == 1 && is(0, WM_CHAR, 0x7F, 0x000E0001, FALSE));
    unsigned events = 0, delivered = 0, dropped = 9;
    rt_input_counts(&events, &delivered, &dropped);
    T_TRUE(events == delivered && dropped == 0);
}
