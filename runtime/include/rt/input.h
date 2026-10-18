/* The scripted input source (core/win_inputsrc.c, built; unit rt-input-script).  A run has
 * nobody at the keyboard: a script of physical events (a mouse moves, a button or a key goes
 * down or up) is played into the window core, which then posts what Windows would post.  The
 * contract, statement by statement with sources, is docs/handoff/rt-contract-input.md ("C12"
 * below is its row); what the real game takes input from is docs/trace/input.md.
 *
 * WITHOUT A SCRIPT OR A HOST NOTHING HERE DOES ANYTHING: no device exists, no message is ever
 * posted, GetRawInputData knows no handle, the key state is 0 and DefWindowProc stops on the
 * input messages, all exactly as before this unit.  rt_input_active() is the one test.
 * A HOST (rt_input_host_attach, below) is a second source of the same events, from a real window.
 *
 * THE SCRIPT.  Plain text, one line an event; `#` starts a comment; numbers are decimal or 0x.
 *     anchor start                  the events below are timed from the moment of loading
 *     anchor present N              ... from the N-th IDXGISwapChain::Present of the run
 *     anchor present +N             ... from the N-th Present after the anchor before fired
 *     anchor cursor-shown           ... from the ShowCursor that makes the pointer visible
 *                                   again (the display counter goes from -1 to 0), which the
 *                                   real game does once, when its menu takes the pointer
 *                                   (docs/trace/input.md, section 4)
 *     anchor mark NAME              ... from the moment the driver reports the mark NAME
 *                                   (rt_input_mark; a name is a-z, 0-9 and `-`).  What a
 *                                   mark means is the driver's: the runtime knows no game.
 *                                   `gate3 run` reports `main-menu` when the game's UI
 *                                   script opens its main menu (gate3/run.c, THE MENU MARK)
 *                                   and `mission-frame` at the first frame of the mission.
 *                                   A mark is a moment: one reported while the script waits
 *                                   for another anchor is not kept
 *     <ms> moveto X Y               the pointer goes to the screen point (X, Y)
 *     <ms> move DX DY               the mouse moves by (DX, DY)
 *     <ms> button left|right|middle down|up
 *     <ms> key down|up VK [SCAN [ext]]     a virtual-key code as the key message carries it
 *                                   (0x10 for either Shift, never 0xA0 to 0xA5).  With no SCAN
 *                                   the event has the scan code and the extended-key flag of
 *                                   the US table (rt/window.h; contract K22, K38; a key the
 *                                   table does not have needs its SCAN); with SCAN it has that,
 *                                   and `ext` only if written.  A held key's repeats are
 *                                   written out: a second `key down` before the `key up` (K6)
 * <ms> is milliseconds after the anchor above the line and may not go backwards inside a
 * section.  A script starts with an anchor line.  Sections are taken in order: an anchor is
 * looked for only once the one before it has fired, and events are delivered in the order of
 * the file.  Anything else in a line is refused when the script is loaded, with the line.
 *
 * DELIVERY.  There is no thread of the runtime's own: events that are due are delivered by
 * the next guest thread that looks at a message queue, waits on one or asks for the cursor
 * (a wait on a queue wakes every 10 ms while a script has events left).  For one event, in
 * this order (C33):
 *   1. the state "now": the pointer moves, clamped to the clip rectangle (C36); the button or
 *      key is down or up for the next events' wParam and repeat bit;
 *   2. WM_INPUT, if the event's collection is registered (C12): to the registration's target,
 *      or to the focus window for a NULL target; wParam RIM_INPUT when that window is the
 *      foreground window, RIM_INPUTSINK when it is not and the registration has
 *      RIDEV_INPUTSINK, and no message otherwise (C13); lParam a new handle whose packet
 *      GetRawInputData gives out (rt/rawinput.h).  A movement's packet has the movement asked
 *      for, before the clip (C6);
 *   3. the legacy message, unless the collection is registered with RIDEV_NOLEGACY (C14):
 *      a mouse message to the window that has the capture, else to the shown top-level
 *      window under the pointer (C21, C22); a key message to the focus window (C27, C28).
 *      With no such window the message is dropped and counted.  An unread WM_MOUSEMOVE of the
 *      same window is replaced (C26).
 * When the owning thread TAKES a mouse message (PeekMessage with PM_REMOVE, GetMessage) the
 * window is first sent WM_NCHITTEST and, with no capture, WM_SETCURSOR (C24); taking a key or
 * button message changes what GetKeyState answers (C35).  WM_CHAR is not the source's: the
 * thread that took a WM_KEYDOWN gets it from its own TranslateMessage (rt_key_translate, K13).
 * A message that guest code posts goes in front of the input that waits (C34).
 *
 * STOPS (a wrong answer must be loud): a key event for VK_MENU or VK_F10, or any key while
 * VK_MENU is down (C29); a press that would be a double click (C31); a child window under
 * the pointer; WM_NCHITTEST answered with anything but HTCLIENT; a peek at a waiting mouse
 * message without PM_REMOVE.  In rt/rawinput.h: GetRawInputData for a handle this runtime did
 * not issue or no longer has, and a buffer that is too small.
 */
#ifndef RT_INPUT_H
#define RT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rt/wintypes.h"

/* The messages and values of the contract that rt/win/user.h does not have (C10, C22 to C25). */
#define WM_SETCURSOR      0x0020
#define WM_NCHITTEST      0x0084
#define WM_INPUT          0x00FF
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_MBUTTONDOWN    0x0207
#define WM_MBUTTONUP      0x0208
#define HTCLIENT          1
#define MK_LBUTTON        0x0001u
#define MK_RBUTTON        0x0002u
#define MK_SHIFT          0x0004u
#define MK_CONTROL        0x0008u
#define MK_MBUTTON        0x0010u

/* Load a script: from a file, or from text (tests).  Once per process; a second load, a file
 * that cannot be read and a line that is not understood are fatal, with the file and line. */
void rt_input_script_load(const char *path);
void rt_input_script_text(const char *name, const char *text);

/* Is a script loaded?  One read of a flag. */
bool rt_input_active(void);

/* The driver's watch on the swap chain's Present calls this with the number of the call. */
void rt_input_present(uint64_t nth);

/* The driver reports a mark: the section that waits for `anchor mark <name>` now fires, and
 * the anchors behind it that can.  Nothing with no script, with another name, or while the
 * script waits for another anchor.  Any thread; posts nothing itself (the events are
 * delivered as always, by the next guest thread that looks at a queue). */
void rt_input_mark(const char *name);

/* A HOST SOURCE: a real window of the host hands physical events to the runtime.  The
 * events mean what a script's events mean and are delivered the same way, by the same
 * code, at the next poll of the input source; their time is the tick of the push. */
enum { RT_HI_MOVETO, RT_HI_MOVE, RT_HI_BUTTON, RT_HI_KEY, RT_HI_ACTIVE };
typedef struct RtHostInput {
    uint8_t kind;          /* RT_HI_* */
    uint8_t down;          /* button, key: 1 down, 0 up */
    uint8_t ext;           /* key: the extended-key flag */
    int32_t a, b;          /* MOVETO: x, y (screen points); MOVE: dx, dy; BUTTON: a = 0 left,
                              1 right, 2 middle; KEY: a = virtual key, b = scan code, or
                              b = 0 to take scan code and flag from the US table.
                              ACTIVE: down 1 when the host's window became the key window,
                              0 when it stopped being it; a, b, ext 0 */
} RtHostInput;
/* From now on the runtime has a mouse and a keyboard: rt_input_active() is true even
 * with no script.  Any thread; calling it twice is harmless. */
void rt_input_host_attach(void);
/* Queue one event.  Any thread; never blocks on guest code; returns false and counts
 * the event as dropped if the queue (4096 events) is full or no host is attached. */
bool rt_input_host_push(const RtHostInput *e);

/* How many events the script and the host pushed (every push counts), how many were
 * delivered, and how many of those found no window for their legacy message; a refused push
 * is dropped.  All 0 with neither a script nor a host. */
void rt_input_counts(unsigned *events, unsigned *delivered, unsigned *dropped);

/* One line with rt_log_raw: "input: script <name>: ...", the counts and what the next event
 * waits for; nothing with no script.  For rt_com_counts_also: takes no lock, allocates
 * nothing. */
void rt_input_report(void);

#endif /* RT_INPUT_H */
