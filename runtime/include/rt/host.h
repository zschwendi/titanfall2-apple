/* The host window (macOS, AppKit): the one window the runtime shows, and the events it hands on.
 * Objective-C in runtime/host/window.m; the translation of its events is rt/hostevent.h.
 * Nothing in here touches the runtime: usable in a program that never called rt_init.
 */
#ifndef RT_HOST_H
#define RT_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "rt/input.h"

typedef struct RtHostWindowDesc {
    uint32_t    width_px, height_px;   /* the client area in pixels (the game's: 1920 by 1080) */
    int32_t     origin_x, origin_y;    /* where that client area is on the runtime's own screen */
    const char *title;                 /* UTF-8 */
    bool        hidden;                /* never shown, never activated: tests */
    bool        fullscreen;            /* a borderless window covering the main screen; the client area is shown scaled
                                          to the largest size of its own shape that fits */
    bool        builtin_screen;        /* full screen goes to the built-in display (the first one that is built in);
                                          false: the main screen (the one with the keyboard focus) */
} RtHostWindowDesc;

/* The process's FIRST thread calls this and stays in it: it makes the application and runs
 * its event loop until rt_host_quit.  Nothing of the guest ever runs on this thread. */
void  rt_host_main(void);
/* Any thread: ends rt_host_main. */
void  rt_host_quit(void);
/* Any thread but the first: creates the one window on the first thread and waits until it
 * exists.  Returns its CAMetalLayer (as void *), or NULL.  A second call returns the same layer. */
void *rt_host_window_open(const RtHostWindowDesc *d);
/* Any thread: the game holds the mouse (true) or lets it go.  With a hidden window this only
 * changes how events are translated; the real cursor is never touched for a hidden window. */
void  rt_host_set_relative(bool on);
/* Where translated events go; the default is rt_input_host_push.  For tests. */
void  rt_host_set_sink(bool (*push)(const RtHostInput *e));
/* Called on the first thread when the person clicks the window's close button (may be NULL). */
void  rt_host_on_close(void (*fn)(void));
/* For tests: the runtime points per host point of the window's picture (2 in every mode but full
 * screen; the full screen's value follows the fit).  Any thread. */
double rt_host_window_scale(void);

#endif /* RT_HOST_H */
