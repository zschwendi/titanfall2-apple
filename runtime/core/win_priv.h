/* Private to runtime/core: what the files of the window and GDI pieces share (public side:
 * rt/window.h, rt/gdi.h; the contract: docs/handoff/rt-contract-window.md).
 *
 *     core/win.c            built   the user table, the lock, the desktop, window records
 *     core/win_msgbox.c     built   rt_message_box
 *     core/win_class.c      built   classes, registered messages, the class lookup below
 *     core/win_queue.c      built   the message queue of a thread (it shares nothing but the lock and the records)
 *     core/win_*.c, gdi_dc.c built  one file per piece (the table at the top of rt/window.h)
 *     core/gdi_object.c     stub    the piece gdi-object
 *
 * Everything declared here is BUILT (core/win.c, core/win_class.c) and tested
 * (tests/core/win_table.c, tests/core/win_class.c).
 */
#ifndef RT_CORE_WIN_PRIV_H
#define RT_CORE_WIN_PRIV_H

#include <stdatomic.h>

#include "core.h"
#include "rt/handle.h"
#include "rt/window.h"
#include "rt/gdi.h"

/* A stop of this unit: RT_WIN_STOP("rt_window_show", "cmd %d would put ...", cmd).  `fn` is
 * the public core function, as a string literal. */
#define RT_WIN_STOP(fn, fmt, ...)  rt_fatal(fn ": not implemented: " fmt, ##__VA_ARGS__)

/* The "user" handle table: 65,536 slots, generations 256 to 511 (rt/handle.h), made at the
 * first call.  Windows, device contexts, GDI objects and hooks go in with rt_htab_new and
 * are found with rt_htab_ref(rt_win_table(), h, RT_K_...). */
RtHandleTable *rt_win_table(void);

/* THE lock of rt/window.h and rt/gdi.h ("LOCKS").  Not recursive.  The handle table has its
 * own lock inside: rt_htab_* may be called with this one held, and without it. */
void rt_win_lock(void);
void rt_win_unlock(void);

#define RT_WIN_MAX_EXTRA    40          /* cbClsExtra, cbWndExtra: what a record holds; more is a stop
                                           (whether Windows refuses more is not settled: review N2) */
#define RT_WIN_MAX_CLASSES  256
#define RT_WIN_MAX_WINDOWS  4096

typedef struct RtWndClass {
    bool      used;
    bool      unicode;                  /* registered with a W function */
    ATOM      atom;
    WCHAR     name[256];                /* as registered; compared without ASCII case */
    UINT      style;
    WNDPROC   proc;
    INT       cls_extra, wnd_extra;
    HINSTANCE instance;
    HICON     icon, icon_sm;
    HCURSOR   cursor;
    HBRUSH    background;
    BYTE      extra[RT_WIN_MAX_EXTRA];
    unsigned  windows;                  /* records of this class that exist */
} RtWndClass;

typedef struct RtWindow {
    RtObject   obj;                     /* kind RT_K_WINDOW */
    HWND       self;
    RtWndClass *cls;
    WNDPROC    proc;                    /* starts as cls->proc; GWLP_WNDPROC replaces it */
    bool       unicode;                 /* created with CreateWindowExW */
    bool       message_only;            /* parent was HWND_MESSAGE: in no list of top-level windows */
    bool       destroying;              /* WM_DESTROY has been sent */
    DWORD      style, exstyle;
    RECT       rect;                    /* top-level: screen coordinates; child: in the parent's client area */
    HWND       parent, owner;           /* at most one is set; both NULL for an unowned top-level window */
    INT_PTR    id;                      /* a child's identifier */
    HINSTANCE  instance;
    LONG_PTR   userdata;
    DWORD      thread_id;               /* the creating guest thread */
    uint64_t   creator;                 /* guest return address of the CreateWindowEx call, for logs */
    WCHAR     *text;                    /* malloc'ed, or NULL for the empty title */
    BYTE       extra[RT_WIN_MAX_EXTRA];
    struct RtWindow *next, *prev;       /* siblings in Z order (win lock) */
    struct RtWindow *up, *first_child;  /* the parent's record, the topmost child (win lock) */
} RtWindow;

/* What the pieces share besides the records.  Every field is read and written under the win
 * lock, except the atomics.  host_away: the host window is not the key window (what the input
 * source's "active off" or RT_HI_ACTIVE says); zero, so the host starts as the key window.
 * host_inactive: the window that rt_win_host_sync deactivated because the host went away, or NULL. */
typedef struct RtWinState {
    _Atomic(HWND) active, focus, foreground, capture;
    _Atomic bool  host_away;
    _Atomic(HWND) host_inactive;
    RtWindow   *top;                    /* first top-level window in Z order */
    RtWindow   *message_only;           /* the message-only windows, newest first */
    unsigned    windows;                /* records that exist */
    RtWndClass  classes[RT_WIN_MAX_CLASSES];
} RtWinState;
extern RtWinState rt_win;

/* ---- classes (core/win_class.c, built) ---------------------------------------------------------- */

/* What guest code passed as a class name or a message name, read out of guest memory once,
 * with no lock held: an atom, or the text as wide characters (narrow text is code page 1252).
 *   RT_KEY_NULL    both pointers NULL             RT_KEY_ATOM   a pointer value below 0x10000: key->atom
 *   RT_KEY_NAME    1 to 255 characters: key->name, terminated, key->atom 0
 *   RT_KEY_EMPTY   the empty string               RT_KEY_LONG   more than 255 characters
 * For every kind but RT_KEY_ATOM and RT_KEY_NAME the key is not to be used. */
typedef struct RtClassKey { ATOM atom; WCHAR name[256]; } RtClassKey;
typedef enum { RT_KEY_NULL = 0, RT_KEY_ATOM, RT_KEY_NAME, RT_KEY_EMPTY, RT_KEY_LONG } RtKeyKind;
RtKeyKind rt_win_text_key(RtText t, RtClassKey *key);

/* The registered class `key` names: by its atom, or by its name without regard to ASCII
 * case.  NULL if there is none; sets no last error.  CALL WITH THE WIN LOCK HELD: the record
 * is a slot of rt_win.classes and stays that class only while the lock is held or while a
 * window of the class exists (rt_win_window_new counts it), so a caller that makes a window
 * looks the class up again under the lock when it links the record. */
RtWndClass *rt_win_class_lookup(const RtClassKey *key);

/* ---- window records (core/win.c, built) --------------------------------------------------------- */

/* A new record with its handle in `w->self`, linked where Windows puts a new window: a
 * top-level window first in rt_win.top (the top of the Z order) and a message-only window
 * first in rt_win.message_only, a child LAST among the children of `parent` (the bottom of
 * its siblings: Wine user32/tests/win.c, test_children_zorder; review D2 of
 * docs/reviews/contracts-check-2.md).  SENDS NO
 * MESSAGE and checks no policy: rt_window_create does both around it, and the tests of the
 * other pieces use it to have a window before rt_window_create is built.
 *   cls         counted: cls->windows goes up, and down when the record is dropped
 *   parent      NULL, HWND_MESSAGE, or a window (then the record is its child if `style`
 *               has WS_CHILD, else it is top-level and `parent` is its owner)
 *   title       copied; NULL for none
 * NULL with ERROR_NOT_ENOUGH_MEMORY when RT_WIN_MAX_WINDOWS records exist or the table is
 * full, and with ERROR_INVALID_WINDOW_HANDLE for a parent that is not a window.  The caller
 * does not hold the win lock. */
typedef struct RtWinNew {
    RtWndClass *cls;
    DWORD       style, exstyle;
    RECT        rect;
    HWND        parent;
    INT_PTR     id;
    HINSTANCE   instance;
    const WCHAR *title;
    bool        unicode;
    DWORD       thread_id;
    uint64_t    creator;
} RtWinNew;
HWND rt_win_window_new(const RtWinNew *args);

/* The record behind `h`, with one more reference (rt_object_unref drops it), or NULL with
 * last error ERROR_INVALID_WINDOW_HANDLE: 0, a stale handle, a kernel handle, a handle of
 * another kind, RT_HWND_DESKTOP and every other constant.  The fields are read under the win
 * lock; the reference only keeps the memory alive after the window is dropped. */
RtWindow *rt_win_window_ref(HWND h);

/* Take the record out of its list and out of the table: its handle is invalid from now on
 * and the memory goes with the last reference.  The caller has sent what had to be sent and
 * dropped the children first.  FALSE with ERROR_INVALID_WINDOW_HANDLE; fatal if the record
 * still has children.  The caller does not hold the win lock. */
BOOL rt_win_window_drop(HWND h);

/* ---- device contexts and GDI objects (records: core/win.c, built; pieces: core/gdi_dc.c, gdi_object.c) */

typedef enum { RT_DC_WINDOW = 1, RT_DC_DISPLAY, RT_DC_MEMORY } RtDcKind;   /* GetDC; CreateDC; CreateCompatibleDC */
typedef enum { RT_GDI_PEN = 0, RT_GDI_BRUSH, RT_GDI_FONT, RT_GDI_BITMAP, RT_GDI_REGION } RtGdiType;

/* A device context: what was selected into it and which attributes were set (rt/gdi.h).
 * Every field is read and written under the win lock. */
typedef struct RtDc {
    RtObject obj;                       /* kind RT_K_DC */
    HDC      self;
    RtDcKind kind;
    HWND     window;                    /* RT_DC_WINDOW: what GetDC was given, NULL for the screen */
    ULONG    text_color, bk_color, bk_mode, text_align, stretch_mode;
    POINT    viewport_org, position;
    HGDIOBJ  sel[4];                    /* by RtGdiType, pen to bitmap: the guest's object, or NULL for the stock default */
    unsigned sel_seq[4], seq;           /* when each slot last left its default: SelectObject(dc, NULL) restores the latest */
} RtDc;

/* A pen, brush, font, bitmap or region: how it was made, which is all GetObject gives back. */
typedef struct RtGdiObj {
    RtObject  obj;                      /* kind RT_K_GDIOBJ */
    HGDIOBJ   self;
    RtGdiType type;
    unsigned  selected;                 /* into how many contexts (a bitmap: at most one); win lock */
    union {
        struct { INT style, width; COLORREF color; } pen;
        struct { COLORREF color; } brush;
        LOGFONTA font;
        struct { INT width, height; UINT bpp; bool dib; void *bits; size_t bytes; DWORD compression, masks[3];
                 LONG x_ppm, y_ppm; } bitmap;                /* a DIB section: `bits` is rt_vm memory, `height` as given (signed) */
        RECT region;
    } u;
} RtGdiObj;

/* A new record of each kind, in the user table, with its handle in `self`.  A context
 * starts with the defaults of rt/gdi.h; an object is zero but for its type: the caller fills
 * `u` under the win lock before it gives the handle out.  NULL with
 * ERROR_NOT_ENOUGH_MEMORY when the table is full.  The record behind a handle, with one more
 * reference (rt_object_unref drops it), or NULL: these two set NO last error.  Closing:
 * rt_htab_close(rt_win_table(), h, RT_K_DC or RT_K_GDIOBJ). */
HDC      rt_win_dc_new(RtDcKind kind, HWND window);
HGDIOBJ  rt_win_gdiobj_new(RtGdiType type);
RtDc     *rt_win_dc_ref(HDC dc);
RtGdiObj *rt_win_gdiobj_ref(HGDIOBJ obj);

/* ---- the first show (core/win_window.c) ---------------------------------------------------------- */

/* Show a hidden top-level window of the calling thread and make it the active, focused,
 * foreground window: the messages of rt_window_show's comment (rt/window.h), sent to the
 * window's procedure.  rt_window_show (core/win_state.c) has checked that this is the case. */
void rt_win_show_top(Ctx *ctx, HWND h);

/* Hide a shown top-level window of the calling thread (ShowWindow(SW_HIDE)): WM_SHOWWINDOW,
 * the two position messages, and its deactivation if it was the active window. */
void rt_win_hide_top(Ctx *ctx, HWND h);

/* ---- the scripted input source (core/win_inputsrc.c; rt/input.h) -------------------------------- */
/* What a queue entry is: 0 a message guest code posted; else one of these, from the source. */
enum { RT_IN_RAW = 1, RT_IN_KEY, RT_IN_MOUSE };
/* Deliver the script's events that are due (one read of a flag with no script).  The win lock
 * is NOT held.  rt_win_input_slice: how long a wait on a queue may sleep before it looks
 * again: INFINITE with no script or none left, else 10 ms. */
void  rt_win_input_poll(void);
DWORD rt_win_input_slice(void);
/* core/win_window.c: the host window's key state (rt_win.host_away) against the windows of the
 * calling thread: when the host stops being key, the active window is deactivated; when it is
 * key again, the window that was deactivated for that is activated.  Cheap when nothing changed.
 * Called by rt_msg_peek after rt_win_input_poll.  The rules and the order of messages: there. */
void  rt_win_host_sync(Ctx *ctx);
/* ShowCursor made the pointer visible (the display counter reached 0 from below). */
void  rt_win_input_cursor_shown(void);
/* The owner of a queue has taken (removed) the input message `m` of kind RT_IN_x: the hit
 * test and WM_SETCURSOR for a mouse message (guest calls: no lock held), the key state. */
void  rt_win_input_taken(Ctx *ctx, const MSG *m, unsigned kind);
/* DefWindowProc for the input messages while a script is loaded: true and *r, or false for a
 * message or a case it does not answer (rt_window_def_proc then stops as before). */
bool  rt_win_input_def_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT *r);
/* core/win_queue.c: append an input message to the queue of the thread that owns `h`, after
 * everything that waits (posted messages go in front of input: C34); an unread WM_MOUSEMOVE
 * of the same window with only WM_INPUT behind it is replaced (C26).  false: `h` is no
 * window.  Stops when the queue holds 10,000 messages. */
bool  rt_win_queue_input(HWND h, UINT msg, WPARAM w, LPARAM l, DWORD time, POINT pt, unsigned kind);
/* core/win_input.c: the pointer's position without a poll; a movement of the mouse itself to
 * (x, y), clamped to the clip, *out where it ended; and GetKeyState's table: `vk` went down
 * (`flip`: it was up before, the toggle bit flips) or up. */
void  rt_win_cursor_peek(POINT *out);
void  rt_win_cursor_move(LONG x, LONG y, POINT *out);
void  rt_win_keystate_note(unsigned vk, bool down, bool flip);
/* One entry of that table as GetKeyboardState would give it: 0x80 down, 1 toggled. */
uint8_t rt_win_keystate_peek(unsigned vk);
#endif /* RT_CORE_WIN_PRIV_H */
