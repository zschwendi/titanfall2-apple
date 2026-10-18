/* The translation of a host window's events into the runtime's input events (rt/hostevent.h;
 * the rules are the brief's rules 1 to 12, which the test restates one per function).
 *   rule 8: a relative movement is scaled by move_scale, not scale: the mouse's feel does not change with
 *           the window's size (full screen included).  Positions (rules 7 and 9) use scale.
 *   rule 12: a key-down with RT_HE_FLAG_COMMAND gives no event and counts as a system key (rule 4);
 *            a key-up is always translated as usual.
 *
 * Nothing here pushes an event: the caller decides what to do with what is written.  The
 * three counts are atomics, so any thread may translate.
 */
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "rt/hostevent.h"
#include "rt/input.h"
#include "rt/mackeys.h"

#define VK_MENU_ALT  0x12   /* VK_MENU: the Option keys on a Mac */
#define VK_F10_KEY   0x79

static atomic_uint c_unknown_key;
static atomic_uint c_system_key;
static atomic_uint c_wheel;

/* Writes the events if they all fit in max; otherwise nothing (rule 11). */
static unsigned put(RtHostInput *out, unsigned max, const RtHostInput *ev, unsigned n)
{
    if (n > max || out == NULL)
        return 0;
    for (unsigned i = 0; i < n; i++)
        out[i] = ev[i];
    return n;
}

/* One key, down or up, by its macOS key code.  Rules 1 to 4. */
static unsigned key_events(uint16_t code, bool down, RtHostInput *out, unsigned max)
{
    uint32_t vk = 0;
    if (!rt_mac_key_to_vk(code, &vk)) {                     /* rule 3 */
        atomic_fetch_add(&c_unknown_key, 1);
        return 0;
    }
    if (vk == VK_MENU_ALT || vk == VK_F10_KEY) {            /* rule 4 */
        atomic_fetch_add(&c_system_key, 1);
        return 0;
    }
    RtHostInput ev = { .kind = RT_HI_KEY, .down = down ? 1 : 0, .ext = 0,
                       .a = (int32_t)vk, .b = 0 };
    if (code == 0x3C) {                                     /* rule 2: right Shift */
        ev.b = 0x36;
    } else if (code == 0x3E) {                              /* rule 2: right Control */
        ev.b = 0x1D;
        ev.ext = 1;
    }
    return put(out, max, &ev, 1);
}

static unsigned flags_event(const RtHostEvent *e, RtHostInput *out, unsigned max)
{
    unsigned code = e->key_code;
    if (code == 0x39) {                                     /* rule 6: Caps Lock */
        RtHostInput ev[2] = {
            { .kind = RT_HI_KEY, .down = 1, .ext = 0, .a = 0x14, .b = 0 },
            { .kind = RT_HI_KEY, .down = 0, .ext = 0, .a = 0x14, .b = 0 },
        };
        return put(out, max, ev, 2);
    }
    if (code == 0x38 || code == 0x3C)                       /* rule 5: Shift keys */
        return key_events((uint16_t)code, (e->flags & RT_HE_FLAG_SHIFT) != 0, out, max);
    if (code == 0x3B || code == 0x3E)                       /* rule 5: Control keys */
        return key_events((uint16_t)code, (e->flags & RT_HE_FLAG_CONTROL) != 0, out, max);
    if (code == 0x3A || code == 0x3D) {                     /* rule 4: Option keys */
        return key_events((uint16_t)code, (e->flags & RT_HE_FLAG_OPTION) != 0, out, max);
    }
    /* rule 3: Command (0x37, 0x36) and anything else with no key */
    atomic_fetch_add(&c_unknown_key, 1);
    return 0;
}

static unsigned mouse_move(const RtHostEvent *e, const RtHostView *v, RtHostInput *out, unsigned max)
{
    if (v->relative) {                                      /* rule 8 */
        long dx = lround(e->dx * v->move_scale);
        long dy = lround(e->dy * v->move_scale);
        if (dx == 0 && dy == 0)
            return 0;
        RtHostInput ev = { .kind = RT_HI_MOVE, .a = (int32_t)dx, .b = (int32_t)dy };
        return put(out, max, &ev, 1);
    }
    RtHostInput ev = { .kind = RT_HI_MOVETO,                /* rule 7 */
                       .a = v->origin_x + (int32_t)lround(e->x * v->scale),
                       .b = v->origin_y + (int32_t)lround(e->y * v->scale) };
    return put(out, max, &ev, 1);
}

static unsigned button_event(const RtHostEvent *e, const RtHostView *v, RtHostInput *out, unsigned max)
{
    if (e->button > 2)                                      /* rule 9: no third-plus button */
        return 0;
    RtHostInput btn = { .kind = RT_HI_BUTTON, .down = e->kind == RT_HE_BUTTON_DOWN ? 1 : 0,
                        .a = e->button };
    if (v->relative)                                        /* rule 9 */
        return put(out, max, &btn, 1);
    RtHostInput at[2] = {
        { .kind = RT_HI_MOVETO,                             /* rule 9: first to the place */
          .a = v->origin_x + (int32_t)lround(e->x * v->scale),
          .b = v->origin_y + (int32_t)lround(e->y * v->scale) },
        btn,
    };
    return put(out, max, at, 2);
}

unsigned rt_host_event_translate(const RtHostEvent *e, const RtHostView *v, RtHostInput *out, unsigned max)
{
    if (e == NULL || v == NULL)
        return 0;
    switch (e->kind) {
    case RT_HE_KEY_DOWN:                                    /* rule 12: Command-key down: rule 4 */
        if (e->flags & RT_HE_FLAG_COMMAND) {
            atomic_fetch_add(&c_system_key, 1);
            return 0;
        }
        return key_events(e->key_code, true, out, max);
    case RT_HE_KEY_UP:      return key_events(e->key_code, false, out, max);
    case RT_HE_FLAGS:       return flags_event(e, out, max);
    case RT_HE_MOUSE_MOVE:  return mouse_move(e, v, out, max);
    case RT_HE_BUTTON_DOWN:
    case RT_HE_BUTTON_UP:   return button_event(e, v, out, max);
    case RT_HE_WHEEL:       atomic_fetch_add(&c_wheel, 1); return 0;   /* rule 10 */
    default:                return 0;
    }
}

void rt_host_event_counts(RtHostEventCounts *out)
{
    if (out == NULL)
        return;
    out->unknown_key = atomic_load(&c_unknown_key);
    out->system_key = atomic_load(&c_system_key);
    out->wheel = atomic_load(&c_wheel);
}
