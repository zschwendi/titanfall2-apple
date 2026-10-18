/* The clipboard of this process (specification: rt/window.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `win-clipboard` (docs/handoff/rt-contract-window.md).  Every function below is
 * declared and specified in rt/window.h; the bodies and the state are here.  Tests: tests/core/win_clipboard.c.
 *
 * State, file-static and read and written only with the win lock held (rt_win_lock): whether the
 * clipboard is open, the host thread that has it open (pthread_t, compared with pthread_equal),
 * the owner it is open with (a value, NULL included), and up to CLIP_MAX pairs of format and
 * handle.  A handle is never read, freed or checked.  A stop is RT_WIN_STOP, called after the
 * lock is released.
 */
#include <pthread.h>

#include "win_priv.h"

#define CLIP_MAX  16                    /* formats the clipboard holds: a 17th is a stop */

static bool      clip_open;             /* win lock */
static pthread_t clip_thread;           /* win lock: the host thread that has it open, while clip_open */
static HWND      clip_owner;            /* win lock: the owner it is open with */
static unsigned  clip_n;                /* win lock: formats set */
static struct { UINT format; HANDLE data; } clip_item[CLIP_MAX];   /* win lock */

/* Win lock held. */
static bool caller_has_open(void) { return clip_open && pthread_equal(clip_thread, pthread_self()); }

static int find_format(UINT format)
{
    for (unsigned i = 0; i < clip_n; i++)
        if (clip_item[i].format == format) return (int)i;
    return -1;
}

static bool is_text(UINT format) { return format == CF_TEXT || format == CF_OEMTEXT || format == CF_UNICODETEXT; }

static bool text_format_set(void)
{
    for (unsigned i = 0; i < clip_n; i++)
        if (is_text(clip_item[i].format)) return true;
    return false;
}

BOOL rt_clipboard_open(HWND owner)
{
    if (owner) {                                    /* the owner is looked at first (V45) */
        RtWindow *w = rt_win_window_ref(owner);
        if (!w) { rt_thread_set_last_error(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
        rt_object_unref(&w->obj);
    }
    enum { OPENED, AGAIN, DENIED, OTHER_WINDOW } how;
    const pthread_t self = pthread_self();
    rt_win_lock();
    if (!clip_open) {
        clip_open = true;
        clip_thread = self;
        clip_owner = owner;
        how = OPENED;
    } else if (pthread_equal(clip_thread, self)) {
        how = clip_owner == owner ? AGAIN : DENIED;     /* the owner it is open with, as a value */
    } else {
        how = (owner && owner == clip_owner) ? OTHER_WINDOW : DENIED;
    }
    rt_win_unlock();
    switch (how) {
    case OPENED:
    case AGAIN:
        return TRUE;
    case DENIED:
        rt_thread_set_last_error(ERROR_ACCESS_DENIED);
        return FALSE;
    default:
        RT_WIN_STOP("rt_clipboard_open", "another thread opens the clipboard with window %p, the one it is open with",
                    (void *)owner);
    }
}

BOOL rt_clipboard_close(void)
{
    rt_win_lock();
    const bool mine = caller_has_open();
    if (mine) { clip_open = false; clip_owner = NULL; }
    rt_win_unlock();
    if (!mine) { rt_thread_set_last_error(ERROR_CLIPBOARD_NOT_OPEN); return FALSE; }
    return TRUE;
}

BOOL rt_clipboard_empty(void)
{
    rt_win_lock();
    const bool mine = caller_has_open();
    if (mine) clip_n = 0;                           /* the handles are forgotten, not freed */
    rt_win_unlock();
    if (!mine) { rt_thread_set_last_error(ERROR_CLIPBOARD_NOT_OPEN); return FALSE; }
    return TRUE;
}

HANDLE rt_clipboard_set(UINT format, HANDLE data)
{
    enum { STORED, NOT_OPEN, FORMAT_ZERO, NO_DATA, FULL } how = STORED;
    rt_win_lock();
    if (!caller_has_open()) how = NOT_OPEN;
    else if (format == 0) how = FORMAT_ZERO;        /* refused although the clipboard is open (D21) */
    else if (!data) how = NO_DATA;
    else {
        const int i = find_format(format);
        if (i >= 0) clip_item[i].data = data;       /* a format that is set is replaced */
        else if (clip_n == CLIP_MAX) how = FULL;
        else { clip_item[clip_n].format = format; clip_item[clip_n].data = data; clip_n++; }
    }
    rt_win_unlock();
    switch (how) {
    case STORED:
        return data;
    case NOT_OPEN:
    case FORMAT_ZERO:
        rt_thread_set_last_error(ERROR_CLIPBOARD_NOT_OPEN);
        return NULL;
    case NO_DATA:
        RT_WIN_STOP("rt_clipboard_set", "format %u with data NULL (Windows: delayed rendering)", (unsigned)format);
    default:
        RT_WIN_STOP("rt_clipboard_set", "format %u, a 17th format (%u formats are set already)",
                    (unsigned)format, (unsigned)CLIP_MAX);
    }
}

HANDLE rt_clipboard_get(UINT format)
{
    HANDLE data = NULL;
    bool mine = false, hit = false, other_text = false;
    rt_win_lock();
    mine = caller_has_open();
    if (mine) {
        const int i = find_format(format);
        if (i >= 0) { data = clip_item[i].data; hit = true; }
        else other_text = is_text(format) && text_format_set();
    }
    rt_win_unlock();
    if (!mine) { rt_thread_set_last_error(ERROR_CLIPBOARD_NOT_OPEN); return NULL; }
    if (hit) return data;
    if (other_text)
        RT_WIN_STOP("rt_clipboard_get", "format %u is not set and another text format is (Windows converts)", (unsigned)format);
    rt_thread_set_last_error(ERROR_NOT_FOUND);
    return NULL;
}
