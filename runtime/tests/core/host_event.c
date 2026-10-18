/* The translation of host events into the runtime's input events (rt/hostevent.h;
 * core/host_event.c).  One function per rule of the brief (rules 1 to 11).
 *
 * The expected values are written here from the rules, by hand; the macOS key codes are
 * those of HIToolbox's Events.h (kVK_*), the Windows keys and scan codes those of the contract
 * (docs/handoff/rt-contract-input.md, K19, K34, K35).
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "rt/test.h"
#include "rt/hostevent.h"
#include "rt/input.h"

static RtHostView view_at(int32_t ox, int32_t oy, double scale, bool relative)
{
    RtHostView v = { .origin_x = ox, .origin_y = oy, .scale = scale, .move_scale = 2.0, .relative = relative };
    return v;
}

static RtHostEvent ev_key(uint8_t kind, uint16_t code)
{
    RtHostEvent e = { .kind = kind, .key_code = code };
    return e;
}

static RtHostEvent ev_flags(uint16_t code, uint32_t flags)
{
    RtHostEvent e = { .kind = RT_HE_FLAGS, .key_code = code, .flags = flags };
    return e;
}

static bool is_key(const RtHostInput *x, uint8_t down, int32_t a, int32_t b, uint8_t ext)
{
    return x->kind == RT_HI_KEY && x->down == down && x->a == a && x->b == b && x->ext == ext;
}

static void counts(RtHostEventCounts *c)
{
    rt_host_event_counts(c);
}

static void rule1_keys(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEvent e = ev_key(RT_HE_KEY_DOWN, 0x00);                 /* kVK_ANSI_A */
    T_CASE("rule 1: A down is one key down, scan and flag left to the source");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x41, 0, 0));
    e.kind = RT_HE_KEY_UP;
    T_CASE("rule 1: A up is one key up");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x41, 0, 0));
    e = ev_key(RT_HE_KEY_DOWN, 0x24);                             /* kVK_Return */
    T_CASE("rule 1: Return down to VK_RETURN");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x0D, 0, 0));
    T_CASE("rule 1: a repeated key down is another key down");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x0D, 0, 0));
    e = ev_key(RT_HE_KEY_DOWN, 0x7B);                             /* kVK_LeftArrow */
    T_CASE("rule 1: Left arrow down to VK_LEFT, extended flag left to the source");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x25, 0, 0));
}

static void rule2_right_modifiers(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEvent e = ev_key(RT_HE_KEY_DOWN, 0x3C);                 /* kVK_RightShift */
    T_CASE("rule 2: right Shift down is VK_SHIFT with scan 0x36, not extended");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x10, 0x36, 0));
    e.kind = RT_HE_KEY_UP;
    T_CASE("rule 2: right Shift up");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x10, 0x36, 0));
    e = ev_key(RT_HE_KEY_DOWN, 0x3E);                             /* kVK_RightControl */
    T_CASE("rule 2: right Control down is VK_CONTROL with scan 0x1D, extended");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x11, 0x1D, 1));
    e.kind = RT_HE_KEY_UP;
    T_CASE("rule 2: right Control up");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x11, 0x1D, 1));
}

static void rule3_unknown_keys(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEventCounts before, after;
    counts(&before);
    RtHostEvent e = ev_key(RT_HE_KEY_DOWN, 0x37);                 /* kVK_Command */
    T_CASE("rule 3: Command down: no event, unknown_key counts one");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    e = ev_key(RT_HE_KEY_UP, 0x37);
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    counts(&after);
    T_TRUE(after.unknown_key == before.unknown_key + 2);
    T_TRUE(after.system_key == before.system_key);
    e = ev_key(RT_HE_KEY_DOWN, 0x3F);                             /* kVK_Function (Fn) */
    T_CASE("rule 3: Fn down: no event");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    e = ev_key(RT_HE_KEY_DOWN, 0x52);                             /* kVK_ANSI_Keypad0 */
    T_CASE("rule 3: keypad 0 down: no event");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    counts(&after);
    T_TRUE(after.unknown_key == before.unknown_key + 4);
}

static void rule4_system_keys(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEventCounts before, after;
    counts(&before);
    RtHostEvent e = ev_key(RT_HE_KEY_DOWN, 0x3A);                 /* kVK_Option */
    T_CASE("rule 4: Option down: no event, system_key counts one");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    e = ev_key(RT_HE_KEY_UP, 0x3D);                               /* kVK_RightOption */
    T_CASE("rule 4: right Option up: no event");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    e = ev_key(RT_HE_KEY_DOWN, 0x6D);                             /* kVK_F10 to VK_F10 */
    T_CASE("rule 4: F10 down: no event");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    counts(&after);
    T_TRUE(after.system_key == before.system_key + 3);
    T_TRUE(after.unknown_key == before.unknown_key);
}

static void rule12_command_keys(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEventCounts before, after;
    counts(&before);
    RtHostEvent e = ev_key(RT_HE_KEY_DOWN, 0x0C);                 /* kVK_ANSI_Q, Command held */
    e.flags = RT_HE_FLAG_COMMAND;
    T_CASE("rule 12: Command-Q down: no event, system_key counts one");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    counts(&after);
    T_TRUE(after.system_key == before.system_key + 1);
    T_TRUE(after.unknown_key == before.unknown_key);
    e = ev_key(RT_HE_KEY_UP, 0x0C);
    e.flags = RT_HE_FLAG_COMMAND;
    T_CASE("rule 12: Command-Q up: the key-up is still delivered");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x51, 0, 0));
}

static void rule5_flags(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEvent e = ev_flags(0x38, RT_HE_FLAG_SHIFT);             /* left Shift, flag set */
    T_CASE("rule 5: left Shift flag set: key down");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x10, 0, 0));
    e = ev_flags(0x38, 0);
    T_CASE("rule 5: left Shift flag clear: key up");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x10, 0, 0));
    e = ev_flags(0x3C, RT_HE_FLAG_SHIFT);                         /* right Shift, flag set */
    T_CASE("rule 5: right Shift flag set: key down with its own scan code");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x10, 0x36, 0));
    e = ev_flags(0x3C, 0);
    T_CASE("rule 5: right Shift flag clear: key up");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x10, 0x36, 0));
    e = ev_flags(0x3B, RT_HE_FLAG_CONTROL);                       /* left Control */
    T_CASE("rule 5: left Control flag set: key down");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x11, 0, 0));
    e = ev_flags(0x3E, RT_HE_FLAG_CONTROL);                       /* right Control */
    T_CASE("rule 5: right Control flag set: key down, scan 0x1D, extended");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 1, 0x11, 0x1D, 1));
    e = ev_flags(0x3E, 0);
    T_CASE("rule 5: right Control flag clear: key up");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x11, 0x1D, 1));
    e = ev_flags(0x38, RT_HE_FLAG_CONTROL);                       /* Shift key, other flag only */
    T_CASE("rule 5: a Shift key is judged by the Shift flag only");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(is_key(&o[0], 0, 0x10, 0, 0));
}

static void rule5_flags_no_event(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEventCounts before, after;
    counts(&before);
    RtHostEvent e = ev_flags(0x3A, RT_HE_FLAG_OPTION);            /* Option: rule 4 */
    T_CASE("rule 5: Option flags change: rule 4, no event, system_key one");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    e = ev_flags(0x37, RT_HE_FLAG_COMMAND);                       /* Command: rule 3 */
    T_CASE("rule 5: Command flags change: rule 3, no event, unknown_key one");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    counts(&after);
    T_TRUE(after.system_key == before.system_key + 1);
    T_TRUE(after.unknown_key == before.unknown_key + 1);
}

static void rule6_caps(void)
{
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostInput o[4];
    RtHostEvent e = ev_flags(0x39, RT_HE_FLAG_CAPS);              /* Caps Lock, flag set */
    T_CASE("rule 6: Caps Lock with its flag set: 0x14 down then 0x14 up");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 2);
    T_TRUE(is_key(&o[0], 1, 0x14, 0, 0));
    T_TRUE(is_key(&o[1], 0, 0x14, 0, 0));
    e = ev_flags(0x39, 0);
    T_CASE("rule 6: Caps Lock with its flag clear: the same two events");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 2);
    T_TRUE(is_key(&o[0], 1, 0x14, 0, 0));
    T_TRUE(is_key(&o[1], 0, 0x14, 0, 0));
}

static void rule7_move_to(void)
{
    RtHostInput o[4];
    RtHostView v = view_at(100, 50, 2.0, false);
    RtHostEvent e = { .kind = RT_HE_MOUSE_MOVE, .x = 10.25, .y = 3.5 };
    T_CASE("rule 7: absolute move: origin + round(point * scale); 20.5 rounds to 21");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(o[0].kind == RT_HI_MOVETO && o[0].a == 121 && o[0].b == 57);
    T_TRUE(o[0].down == 0 && o[0].ext == 0);
    e.x = 0.0;
    e.y = 0.0;
    T_CASE("rule 7: absolute move to the origin of the view");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(o[0].kind == RT_HI_MOVETO && o[0].a == 100 && o[0].b == 50);
}

static void rule8_move_relative(void)
{
    RtHostInput o[4];
    RtHostView v = view_at(100, 50, 2.0, true);
    RtHostEvent e = { .kind = RT_HE_MOUSE_MOVE, .dx = 1.25, .dy = -0.25 };
    T_CASE("rule 8: relative move: round(delta * scale); 2.5 to 3, -0.5 to -1");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(o[0].kind == RT_HI_MOVE && o[0].a == 3 && o[0].b == -1);
    e.dx = 0.2;
    e.dy = 0.1;
    T_CASE("rule 8: relative move rounding to 0 in both: no event");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    e.dx = 0.2;
    e.dy = 4.0;
    T_CASE("rule 8: relative move 0 in one only: one event with the 0");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(o[0].kind == RT_HI_MOVE && o[0].a == 0 && o[0].b == 8);
}

/* The full screen: positions scale by the fit (1920 / 1512), relative moves stay at 2 per point. */
static void fullscreen_scale(void)
{
    RtHostInput o[4];
    RtHostView v = view_at(5, 28, 1920.0 / 1512.0, false);
    v.move_scale = 2.0;
    RtHostEvent e = { .kind = RT_HE_MOUSE_MOVE, .x = 756.0, .y = 425.0 };
    T_CASE("full screen: a position (756, 425) in the view is origin + (960, 540) (within a pixel)");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 1);
    T_TRUE(o[0].kind == RT_HI_MOVETO && abs(o[0].a - (5 + 960)) <= 1 && abs(o[0].b - (28 + 540)) <= 1);
    RtHostView r = view_at(5, 28, 1920.0 / 1512.0, true);
    r.move_scale = 2.0;
    RtHostEvent m = { .kind = RT_HE_MOUSE_MOVE, .dx = 3.0, .dy = -2.0 };
    T_CASE("full screen: a relative movement of (3, -2) is still (6, -4)");
    T_TRUE(rt_host_event_translate(&m, &r, o, 4) == 1);
    T_TRUE(o[0].kind == RT_HI_MOVE && o[0].a == 6 && o[0].b == -4);
}

static void rule9_buttons(void)
{
    RtHostInput o[4];
    RtHostView v = view_at(10, 20, 1.0, false);
    RtHostEvent e = { .kind = RT_HE_BUTTON_DOWN, .button = 1, .x = 5.0, .y = 6.0 };
    T_CASE("rule 9: right button down, absolute: MOVETO to its place, then the button");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 2);
    T_TRUE(o[0].kind == RT_HI_MOVETO && o[0].a == 15 && o[0].b == 26);
    T_TRUE(o[1].kind == RT_HI_BUTTON && o[1].a == 1 && o[1].down == 1);
    e.kind = RT_HE_BUTTON_UP;
    e.button = 0;
    T_CASE("rule 9: left button up, absolute");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 2);
    T_TRUE(o[1].kind == RT_HI_BUTTON && o[1].a == 0 && o[1].down == 0);
    e.kind = RT_HE_BUTTON_DOWN;
    e.button = 2;
    T_CASE("rule 9: middle button down, absolute");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 2);
    T_TRUE(o[1].kind == RT_HI_BUTTON && o[1].a == 2 && o[1].down == 1);

    RtHostView r = view_at(10, 20, 1.0, true);
    e.button = 1;
    T_CASE("rule 9: relative: only the button, no MOVETO");
    T_TRUE(rt_host_event_translate(&e, &r, o, 4) == 1);
    T_TRUE(o[0].kind == RT_HI_BUTTON && o[0].a == 1 && o[0].down == 1);

    e.button = 3;
    T_CASE("rule 9: a button above 2: no event");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    T_TRUE(rt_host_event_translate(&e, &r, o, 4) == 0);
}

static void rule10_wheel(void)
{
    RtHostInput o[4];
    RtHostView v = view_at(0, 0, 1.0, false);
    RtHostEventCounts before, after;
    counts(&before);
    RtHostEvent e = { .kind = RT_HE_WHEEL };
    T_CASE("rule 10: wheel: no event, wheel counts one");
    T_TRUE(rt_host_event_translate(&e, &v, o, 4) == 0);
    counts(&after);
    T_TRUE(after.wheel == before.wheel + 1);
    T_TRUE(after.unknown_key == before.unknown_key);
    T_TRUE(after.system_key == before.system_key);
}

static void rule11_max(void)
{
    RtHostView v = view_at(10, 20, 1.0, false);
    RtHostEvent e = { .kind = RT_HE_BUTTON_DOWN, .button = 1, .x = 5.0, .y = 6.0 };
    RtHostInput o[2];
    o[0].kind = 0xEE;
    o[0].a = 0x5A5A;
    T_CASE("rule 11: rule 9 with max 1: nothing written, 0 returned");
    T_TRUE(rt_host_event_translate(&e, &v, o, 1) == 0);
    T_TRUE(o[0].kind == 0xEE && o[0].a == 0x5A5A);

    RtHostEvent c = ev_flags(0x39, RT_HE_FLAG_CAPS);
    T_CASE("rule 11: Caps Lock (two events) with max 1: nothing written, 0 returned");
    T_TRUE(rt_host_event_translate(&c, &v, o, 1) == 0);
    T_TRUE(o[0].kind == 0xEE && o[0].a == 0x5A5A);

    RtHostEvent m = { .kind = RT_HE_MOUSE_MOVE, .x = 1.0, .y = 1.0 };
    T_CASE("rule 11: max 0: nothing written, 0 returned");
    T_TRUE(rt_host_event_translate(&m, &v, o, 0) == 0);
    T_TRUE(o[0].kind == 0xEE);
}

TEST(core, host_event)
{
    rule1_keys();
    rule2_right_modifiers();
    rule3_unknown_keys();
    rule4_system_keys();
    rule5_flags();
    rule5_flags_no_event();
    rule6_caps();
    rule7_move_to();
    rule8_move_relative();
    fullscreen_scale();
    rule9_buttons();
    rule10_wheel();
    rule11_max();
    rule12_command_keys();
}
