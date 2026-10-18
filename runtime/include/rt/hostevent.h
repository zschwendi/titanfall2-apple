/* The translation of a host window's events (a macOS key, mouse or button event, copied
 * into RtHostEvent by the Objective-C side) into the runtime's input events (RtHostInput,
 * rt/input.h "A HOST SOURCE").  Plain C; knows nothing of AppKit.  Rules: core/host_event.c.
 */
#ifndef RT_HOSTEVENT_H
#define RT_HOSTEVENT_H

#include <stdbool.h>
#include <stdint.h>

#include "rt/input.h"

enum { RT_HE_KEY_DOWN, RT_HE_KEY_UP, RT_HE_FLAGS, RT_HE_MOUSE_MOVE, RT_HE_BUTTON_DOWN,
       RT_HE_BUTTON_UP, RT_HE_WHEEL };
/* macOS modifier flag bits (NSEventModifierFlags), as numbers so that no framework is needed. */
#define RT_HE_FLAG_CAPS    (1u << 16)
#define RT_HE_FLAG_SHIFT   (1u << 17)
#define RT_HE_FLAG_CONTROL (1u << 18)
#define RT_HE_FLAG_OPTION  (1u << 19)
#define RT_HE_FLAG_COMMAND (1u << 20)

typedef struct RtHostEvent {
    uint8_t  kind;       /* RT_HE_* */
    uint8_t  button;     /* BUTTON_*: 0 left, 1 right, 2 middle */
    uint16_t key_code;   /* KEY_*, FLAGS: the macOS virtual key code of the key */
    uint32_t flags;      /* FLAGS: the modifier flags after the change */
    double   x, y;       /* MOUSE_MOVE, BUTTON_*: place in the view, in points, origin top left */
    double   dx, dy;     /* MOUSE_MOVE: the device's own movement, in points */
} RtHostEvent;

typedef struct RtHostView {
    int32_t origin_x, origin_y;   /* where the view's top left is on the runtime's screen, in its points */
    double  scale;                /* runtime points per host point, for positions (2 for our window; less than 2 full screen) */
    double  move_scale;           /* runtime counts per host point of relative movement; 2 in every mode, so that the feel of the mouse does not change with the window's size */
    bool    relative;             /* the game holds the mouse: movement is told as deltas */
} RtHostView;

/* What the translation did not pass on, counted since the process began. */
typedef struct RtHostEventCounts { unsigned unknown_key, system_key, wheel; } RtHostEventCounts;

/* Writes at most `max` runtime events for one host event into `out` and returns how many.
 * Pushes nothing itself.  Any thread. */
unsigned rt_host_event_translate(const RtHostEvent *e, const RtHostView *v, RtHostInput *out, unsigned max);
void rt_host_event_counts(RtHostEventCounts *out);

#endif /* RT_HOSTEVENT_H */
