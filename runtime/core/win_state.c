/* Show, paint state, focus, capture (specification: rt/window.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `win-state` (docs/handoff/rt-contract-window.md).  Every function below is
 * declared and specified in rt/window.h.  Tests: tests/core/win_state.c.
 *
 * STATE: none of its own.  The focus, active, foreground and capture windows are the four
 * atomics of rt_win (core/win_priv.h).  This piece changes only the capture, and only with the
 * win lock held: setting it depends on what it held before.  rt_window_show checks its case and
 * leaves the messages to core/win_window.c (rt_win_show_top, rt_win_hide_top); a record has no
 * update region.
 *
 * MAY CALL: rt_win_window_ref and rt_object_unref (a record, by its handle), rt_win_lock and
 * rt_win_unlock, rt_thread_self, and RT_WIN_STOP, which is called only after the lock is
 * released and the reference dropped.
 */
#include "win_priv.h"

/* Whether `h` names a window.  rt_win_window_ref sets ERROR_INVALID_WINDOW_HANDLE when it does not. */
static BOOL is_window(HWND h)
{
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return FALSE;
    rt_object_unref(&w->obj);
    return TRUE;
}

BOOL rt_window_show(HWND h, INT cmd)
{
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return FALSE;
    rt_win_lock();
    const bool visible = (w->style & WS_VISIBLE) != 0, top = !w->up && !w->message_only;
    const DWORD tid = w->thread_id;
    const ATOM atom = w->cls->atom;
    const uint64_t creator = w->creator;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    const bool shows = cmd == SW_SHOWNORMAL || cmd == SW_SHOW || cmd == SW_SHOWDEFAULT;
    if (cmd == SW_HIDE && !visible) return FALSE;              /* nothing is sent, from any thread */
    if (shows && visible) return TRUE;                          /* shown and normal already: nothing is sent */
    if (cmd != SW_HIDE && !shows)
        RT_WIN_STOP("rt_window_show", "cmd %d (class atom 0x%04x, created at 0x%llx): only SW_HIDE, SW_SHOWNORMAL, SW_SHOW and "
                    "SW_SHOWDEFAULT are built", cmd, (unsigned)atom, (unsigned long long)creator);
    if (!top) RT_WIN_STOP("rt_window_show", "cmd %d for a child or a message-only window (class atom 0x%04x)", cmd, (unsigned)atom);
    if (tid != rt_thread_self()->id)
        RT_WIN_STOP("rt_window_show", "cmd %d for a window of another thread (0x%x)", cmd, (unsigned)tid);
    if (shows) { rt_win_show_top(&rt_thread_self()->ctx, h); return FALSE; }
    rt_win_hide_top(&rt_thread_self()->ctx, h);
    return TRUE;
}

BOOL rt_window_update(HWND h)
{
    return is_window(h);                        /* a record has no update region: nothing to paint */
}

BOOL rt_window_invalidate(HWND h, const RECT *rc, BOOL erase)
{
    (void)rc; (void)erase;
    if (!h) return TRUE;                        /* all windows: nothing is painted */
    return is_window(h);
}

BOOL rt_window_validate(HWND h, const RECT *rc)
{
    (void)rc;
    if (!h) return TRUE;
    return is_window(h);
}

HWND rt_window_set_focus(HWND h)
{
    if (!h) return atomic_exchange(&rt_win.focus, NULL);   /* the previous focus; none is left */
    if (is_window(h)) RT_WIN_STOP("rt_window_set_focus", "activation needs a visible window");
    return NULL;
}

HWND rt_window_set_active(HWND h)
{
    if (h && is_window(h)) RT_WIN_STOP("rt_window_set_active", "activation needs a visible window");
    return NULL;
}

BOOL rt_window_set_foreground(HWND h)
{
    if (!is_window(h)) return FALSE;
    RT_WIN_STOP("rt_window_set_foreground", "activation needs a visible window");
}

HWND rt_window_set_capture(HWND h)
{
    RtWindow *w = rt_win_window_ref(h);
    if (!w)
        RT_WIN_STOP("rt_window_set_capture", "a handle that is not a window (0x%llx)",
                    (unsigned long long)(uintptr_t)h);
    const DWORD self = rt_thread_self()->id;
    rt_win_lock();
    const DWORD owner = w->thread_id;
    const HWND held = atomic_load(&rt_win.capture);
    if (owner == self && !held) atomic_store(&rt_win.capture, h);   /* held by h already: changes nothing */
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (owner != self) {
        /* rt/window.h says where this answer comes from.  Nothing was changed above. */
        static _Atomic bool said;
        if (!atomic_exchange(&said, true))
            rt_log(RT_CH_WINDOW, RT_LL_WARN, "SetCapture(0x%llx): the window belongs to thread 0x%x and the caller is thread 0x%x: "
                   "NULL with ERROR_ACCESS_DENIED, the capture is not changed (said once)",
                   (unsigned long long)(uintptr_t)h, (unsigned)owner, (unsigned)self);
        rt_thread_set_last_error(ERROR_ACCESS_DENIED);
        return NULL;
    }
    if (held && held != h)
        RT_WIN_STOP("rt_window_set_capture", "a capture change while another window (0x%llx) holds it",
                    (unsigned long long)(uintptr_t)held);
    return held;
}

BOOL rt_window_release_capture(void)
{
    rt_win_lock();
    atomic_store(&rt_win.capture, NULL);
    rt_win_unlock();
    return TRUE;                                /* also when nothing was captured */
}
