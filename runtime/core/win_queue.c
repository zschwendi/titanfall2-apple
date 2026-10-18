/* The message queue of a thread (specification: rt/window.h; private side: core/win_priv.h).
 *
 * One RtMsgQueue per guest thread that ever peeked, got, waited or was posted to, found by
 * the thread's id in a list that only grows.  Thread ids are never reused (rt/thread.h), so a
 * queue is never taken for another thread's; and a queue is never freed, because freeing it
 * when its thread ends needs a call in the thread unit's exit path, which this piece does not
 * edit (a queue is one small object and its array).  No slot of RtThread.sub is used.
 *
 * A queued message keeps a reference to the record of its window.  A record whose window was
 * dropped has `self` NULL (core/win.c): such a message is thrown away the next time the
 * queue is looked at, which is what destroying a window does to its posted messages on
 * Windows (DestroyWindow's page: it "flushes the thread message queue"; from memory).
 *
 * LOCKS.  The list, every queue's messages, `warned` and `last_time`: the win lock.  `fresh`
 * and `told`: the dispatcher lock (rt/handle.h), because a queue is a waitable object
 * (RT_K_MSGQUEUE, never in a handle table) and the wait core asks is_signaled under that
 * lock.  Order: the win lock first, the dispatcher lock inside it.  A post appends and
 * signals inside one hold of the win lock, and a look clears `fresh` and scans inside one,
 * so "new since the owner last looked" is exact: a wait is never woken for a message the
 * owner has already seen, and never sleeps on one it has not.
 */
#include <stdlib.h>
#include <string.h>

#include "win_priv.h"
#include "rt/thread.h"
#include "rt/time.h"

#define RT_MSG_MAX  10000u          /* PostMessage's page: "at most 10,000 messages" (review V9) */

typedef struct Entry {
    MSG       m;
    RtWindow *win;                  /* the record of m.hwnd, referenced; NULL for a thread message */
    uint8_t   input;                /* 0: posted by guest code; RT_IN_x: from the scripted input source (rt/input.h) */
} Entry;

typedef struct RtMsgQueue {
    RtObject  obj;                  /* kind RT_K_MSGQUEUE */
    DWORD     thread_id;            /* the owner; never changes */
    struct RtMsgQueue *next;
    Entry    *e;                    /* oldest first */
    unsigned  count, cap;
    bool      warned;               /* rt_msg_get's line about an empty queue was written */
    LONG      last_time;            /* MSG.time of the last message the owner removed */
    bool      fresh;                /* a message arrived since the owner last peeked or got */
    bool      told;                 /* a wait returned for the queue and the owner has not looked since */
} RtMsgQueue;

static RtMsgQueue *queues;

static bool queue_signaled(RtObject *obj, struct RtThread *thread) {
    (void)thread;
    return ((RtMsgQueue *)obj)->fresh;
}

/* No consume: a wait takes nothing from a queue.  Only a look (peek, get) makes a message old. */
static const RtObjectOps queue_ops = { .name = "message queue", .is_signaled = queue_signaled };

static DWORD self_id(Ctx *ctx, const char *fn) {
    if (!ctx || !RT_THREAD(ctx)) rt_fatal("%s: no ctx, or a ctx that belongs to no thread", fn);
    return RT_THREAD(ctx)->id;
}

static RtMsgQueue *find_locked(DWORD tid) {
    RtMsgQueue *q = queues;
    while (q && q->thread_id != tid) q = q->next;
    return q;
}

/* The queue of thread `tid`, made if it has none. */
static RtMsgQueue *queue_of(DWORD tid) {
    rt_win_lock();
    RtMsgQueue *q = find_locked(tid);
    rt_win_unlock();
    if (q) return q;
    RtMsgQueue *n = (RtMsgQueue *)rt_object_new(RT_K_MSGQUEUE, sizeof *n, &queue_ops, NULL, NULL);
    if (!n) rt_fatal("win-queue: no host memory for the message queue of thread 0x%x", (unsigned)tid);
    n->thread_id = tid;
    rt_win_lock();
    q = find_locked(tid);                           /* a poster and the owner may both come first */
    if (!q) { n->next = queues; queues = n; q = n; n = NULL; }
    rt_win_unlock();
    if (n) rt_object_unref(&n->obj);
    return q;
}

/* Take entry `i` out (win lock held).  The caller drops the entry's window reference. */
static void remove_at(RtMsgQueue *q, unsigned i) {
    memmove(&q->e[i], &q->e[i + 1], (q->count - i - 1) * sizeof q->e[0]);
    q->count--;
}

/* MSG.pt of a message posted now: the cursor position (MSG's page; review V38).  Read without
 * the win lock held, and without delivering input. */
static void post_point(POINT *pt) {
    rt_win_cursor_peek(pt);
}

/* What rt_msg_peek and rt_msg_get share.  1: the oldest message that passes is in *out (and
 * out of the queue if `remove`); 0: there is none; -1: `h` is not a window, last error set.
 * Every return but -1 has made the queue's messages old. */
static int look(Ctx *ctx, const char *fn, RtMsgQueue *q, MSG *out, HWND h, UINT min, UINT max, bool remove) {
    if (min > 0xFFFF || max > 0xFFFF)
        rt_fatal("%s: not implemented: a message range 0x%x to 0x%x (the high word is reserved for the system)", fn, min, max);
    if (min > max) rt_fatal("%s: not implemented: a message range whose first, 0x%x, is above its last, 0x%x", fn, min, max);
    bool ranged = min || max, thread_only = h == (HWND)(intptr_t)-1;
    RtWindow *only = NULL;
    if ((uintptr_t)h == 1 || (uintptr_t)h == 0xFFFF)
        rt_fatal("%s: not implemented: window filter %p (Windows reads it as -1)", fn, (void *)h);
    if (h == RT_HWND_DESKTOP) rt_fatal("%s: not implemented: the desktop window as the filter, a window of another thread", fn);
    if (h && !thread_only) {
        only = (RtWindow *)rt_htab_ref(rt_win_table(), h, RT_K_WINDOW);
        if (!only) { rt_set_last_error(ctx, ERROR_INVALID_WINDOW_HANDLE); return -1; }
    }

    MSG m;
    RtWindow *gone = NULL;
    unsigned input = 0;
    bool found = false, quit_behind = false, foreign = false, dropped = false;
    rt_win_lock();
    if (only && !only->self) dropped = true;                    /* destroyed since the lookup */
    else if (only && only->thread_id != q->thread_id) foreign = true;
    else {
        rt_dispatcher_lock();
        q->fresh = q->told = false;
        rt_dispatcher_unlock();
        for (unsigned i = 0; i < q->count; ) {
            Entry *e = &q->e[i];
            if (e->win && !e->win->self) {                      /* its window is gone: so is the message */
                rt_object_unref(&e->win->obj);                  /* frees at most a title: no lock is taken */
                remove_at(q, i);
                continue;
            }
            bool mine = true;
            if (thread_only) mine = !e->win;
            else if (only) {                                    /* the window or one below it (IsChild) */
                RtWindow *w = e->win;
                while (w && w != only) w = w->up;
                mine = w != NULL;
            }
            if (mine && (!ranged || (e->m.message >= min && e->m.message <= max))) {
                m = e->m;
                found = true;
                input = e->input;
                if (remove) { gone = e->win; q->last_time = (LONG)m.time; remove_at(q, i); }
                break;
            }
            if (mine && e->m.message == WM_QUIT) { quit_behind = true; break; }
            i++;
        }
    }
    rt_win_unlock();
    if (only) rt_object_unref(&only->obj);
    if (gone) rt_object_unref(&gone->obj);
    if (dropped) { rt_set_last_error(ctx, ERROR_INVALID_WINDOW_HANDLE); return -1; }
    if (foreign) rt_fatal("%s: not implemented: window filter %p is a window of another thread (what Windows answers is "
                          "not verified)", fn, (void *)h);
    if (quit_behind)
        rt_fatal("%s: not implemented: a posted WM_QUIT is in the queue and the range 0x%x to 0x%x leaves it out "
                 "(the page says WM_QUIT is always retrieved; in which order is not verified)", fn, min, max);
    if (!found) return 0;
    if (input == RT_IN_MOUSE && !remove)
        rt_fatal("%s: not implemented: a look without PM_REMOVE at a waiting mouse message 0x%x of the input script (Windows runs "
                 "the hit test at the look; what it sends again at the next one is not built)", fn, m.message);
    if (input && remove) rt_win_input_taken(ctx, &m, input);    /* WM_NCHITTEST, WM_SETCURSOR, the key state: no lock is held */
    /* Member by member: the padding of a MSG is not written (whether Windows writes it is not verified). */
    out->hwnd = m.hwnd;  out->message = m.message;  out->wParam = m.wParam;  out->lParam = m.lParam;
    out->time = m.time;  out->pt = m.pt;
    return 1;
}

BOOL rt_msg_peek(Ctx *ctx, MSG *out, HWND h, UINT min, UINT max, UINT flags, bool unicode)
{
    (void)unicode;
    DWORD self = self_id(ctx, "rt_msg_peek");
    if (!out) RT_WIN_STOP("rt_msg_peek", "a NULL MSG pointer");
    if (flags & ~(PM_REMOVE | PM_NOYIELD))
        RT_WIN_STOP("rt_msg_peek", "flags 0x%x: only PM_REMOVE and PM_NOYIELD are built", flags);
    rt_win_input_poll();
    rt_win_host_sync(ctx);
    return look(ctx, "rt_msg_peek", queue_of(self), out, h, min, max, (flags & PM_REMOVE) != 0) == 1;
}

INT rt_msg_get(Ctx *ctx, MSG *out, HWND h, UINT min, UINT max, bool unicode)
{
    (void)unicode;
    DWORD self = self_id(ctx, "rt_msg_get");
    if (!out) RT_WIN_STOP("rt_msg_get", "a NULL MSG pointer");
    RtMsgQueue *q = queue_of(self);
    for (;;) {
        rt_win_input_poll();
        int r = look(ctx, "rt_msg_get", q, out, h, min, max, true);
        if (r < 0) return -1;
        if (r > 0) return out->message == WM_QUIT ? 0 : 1;
        rt_win_lock();
        bool first = !q->warned;
        q->warned = true;
        rt_win_unlock();
        if (first) rt_log(RT_CH_WINDOW, RT_LL_WARN, "GetMessage: thread 0x%x waits on an empty queue", (unsigned)self);
        /* The look made every message old, so this sleeps until the next post and no post is
         * missed: one that came after the look has set `fresh` already. */
        RtObject *o = &q->obj;
        const DWORD slice = rt_win_input_slice();   /* INFINITE unless an input script has events left */
        DWORD w = rt_wait(ctx, 1, &o, false, slice, false);
        if (w == WAIT_TIMEOUT && slice != INFINITE) continue;
        if (w != WAIT_OBJECT_0) rt_fatal("rt_msg_get: the wait on the queue of thread 0x%x returned 0x%x", (unsigned)self, (unsigned)w);
    }
}

/* PostMessage refuses a system message that carries a pointer (ERROR_MESSAGE_SYNC_ONLY; review
 * V38) and converts the character of a character message between the A and W forms.  Neither
 * is built: this lets through what needs neither, and stops for the rest. */
static void check_postable(UINT msg, WPARAM w) {
    if (msg > 0xFFFF) RT_WIN_STOP("rt_msg_post", "message 0x%x, above 0xFFFF", msg);
    if (msg >= WM_USER) return;                     /* class, application and registered messages: never touched */
    bool is_char = msg == WM_CHAR || msg == 0x103 || msg == 0x106 || msg == 0x107;      /* and the dead and system forms */
    if (is_char && w >= 0x80)
        RT_WIN_STOP("rt_msg_post", "character message 0x%x with character 0x%llx: the A to W conversion is not built",
                    msg, (unsigned long long)w);
    if (msg == WM_NULL || msg == WM_CLOSE || msg == WM_QUIT || msg == 0x111 /* WM_COMMAND */ || msg == 0x112 /* WM_SYSCOMMAND */
        || (msg >= WM_KEYDOWN && msg <= 0x107) || (msg >= 0x200 && msg <= 0x20E /* the mouse messages */))
        return;
    RT_WIN_STOP("rt_msg_post", "system message 0x%x: whether Windows posts it or refuses it as one that carries a "
                "pointer is not built", msg);
}

BOOL rt_msg_post(Ctx *ctx, HWND h, UINT msg, WPARAM w, LPARAM l)
{
    DWORD self = self_id(ctx, "rt_msg_post");
    if (h == HWND_BROADCAST) RT_WIN_STOP("rt_msg_post", "HWND_BROADCAST: a post to every top-level window");
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_msg_post", "a post to the desktop window");
    if (h == (HWND)(intptr_t)-1) RT_WIN_STOP("rt_msg_post", "a post to (HWND)-1 (Wine reads it as HWND_BROADCAST; not verified)");
    check_postable(msg, w);
    RtWindow *win = NULL;
    DWORD tid = self;
    if (h) {
        win = (RtWindow *)rt_htab_ref(rt_win_table(), h, RT_K_WINDOW);
        if (!win) { rt_set_last_error(ctx, ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
        tid = win->thread_id;                       /* set before the record has a handle, never changed */
    }
    RtMsgQueue *q = queue_of(tid);
    Entry e = { .m = { .hwnd = h, .message = msg, .wParam = w, .lParam = l, .time = (DWORD)rt_time_tick_ms() }, .win = win };
    post_point(&e.m.pt);

    bool dropped = false, full = false;
    unsigned count = 0;
    rt_win_lock();
    if (win && !win->self) dropped = true;          /* destroyed since the lookup */
    else if (q->count == RT_MSG_MAX) full = true;
    else {
        if (q->count == q->cap) {
            unsigned cap = q->cap ? q->cap * 2 : 16;
            Entry *grown = realloc(q->e, cap * sizeof *grown);
            if (!grown) { rt_win_unlock(); rt_fatal("rt_msg_post: no host memory for %u messages", cap); }
            q->e = grown;
            q->cap = cap;
        }
        unsigned at = 0;                            /* in front of the input that waits (rt/input.h; C34): the end, with no script */
        while (at < q->count && !q->e[at].input) at++;
        memmove(&q->e[at + 1], &q->e[at], (q->count - at) * sizeof q->e[0]);
        q->e[at] = e;
        count = ++q->count;
        rt_dispatcher_lock();
        q->fresh = true;
        rt_signal(&q->obj);
        rt_dispatcher_unlock();
    }
    rt_win_unlock();
    if (full) RT_WIN_STOP("rt_msg_post", "the queue of thread 0x%x holds %u messages, the Windows limit", (unsigned)tid, RT_MSG_MAX);
    if (dropped) {
        rt_object_unref(&win->obj);
        rt_set_last_error(ctx, ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    rt_log(RT_CH_WINDOW, RT_LL_DEBUG, "post: thread 0x%x posts message 0x%x (%llx, %llx) for window %p to the queue of thread 0x%x, "
           "which now holds %u", (unsigned)self, msg, (unsigned long long)w, (unsigned long long)l, (void *)h, (unsigned)tid, count);
    return TRUE;
}

bool rt_win_queue_input(HWND h, UINT msg, WPARAM w, LPARAM l, DWORD time, POINT pt, unsigned kind)
{
    RtWindow *win = (RtWindow *)rt_htab_ref(rt_win_table(), h, RT_K_WINDOW);
    if (!win) return false;
    RtMsgQueue *q = queue_of(win->thread_id);
    const Entry e = { .m = { .hwnd = h, .message = msg, .wParam = w, .lParam = l, .time = time, .pt = pt }, .win = win, .input = (uint8_t)kind };
    RtWindow *replaced = NULL;
    bool ok = false, full = false;
    rt_win_lock();
    if (win->self) {
        if (msg == 0x0200 /* WM_MOUSEMOVE */)       /* C26: the unread movement of this window, behind which only WM_INPUT waits */
            for (unsigned i = q->count; i-- > 0; ) {
                if (q->e[i].input == RT_IN_RAW) continue;
                if (q->e[i].input == RT_IN_MOUSE && q->e[i].m.message == msg && q->e[i].win == win) { replaced = win; remove_at(q, i); }
                break;
            }
        if (q->count == RT_MSG_MAX) full = true;
        else {
            if (q->count == q->cap) {
                const unsigned cap = q->cap ? q->cap * 2 : 16;
                Entry *grown = realloc(q->e, cap * sizeof *grown);
                if (!grown) { rt_win_unlock(); rt_fatal("rt_win_queue_input: no host memory for %u messages", cap); }
                q->e = grown;
                q->cap = cap;
            }
            q->e[q->count++] = e;
            rt_dispatcher_lock();
            q->fresh = true;
            rt_signal(&q->obj);
            rt_dispatcher_unlock();
            ok = true;
        }
    }
    rt_win_unlock();
    if (replaced) rt_object_unref(&replaced->obj);
    if (full) RT_WIN_STOP("rt_win_queue_input", "the queue of thread 0x%x holds %u messages, the Windows limit, and the input script has "
                          "more for it: nobody takes its messages", (unsigned)win->thread_id, RT_MSG_MAX);
    if (!ok) rt_object_unref(&win->obj);
    return ok;
}

DWORD rt_msg_wait(Ctx *ctx, DWORD n, const HANDLE *handles, BOOL wait_all, DWORD ms, DWORD wake_mask)
{
    DWORD self = self_id(ctx, "rt_msg_wait");
    if (!(wake_mask & QS_POSTMESSAGE))
        RT_WIN_STOP("rt_msg_wait", "wake mask 0x%x without QS_POSTMESSAGE: only posted messages exist", (unsigned)wake_mask);
    if (n > MAXIMUM_WAIT_OBJECTS - 1) RT_WIN_STOP("rt_msg_wait", "%u handles, more than MAXIMUM_WAIT_OBJECTS minus one", (unsigned)n);
    if (wait_all && n > 0) RT_WIN_STOP("rt_msg_wait", "a wait for all of %u handles and the queue", (unsigned)n);
    if (n && !handles) RT_WIN_STOP("rt_msg_wait", "%u handles with a NULL handle array", (unsigned)n);
    for (DWORD i = 0; i < n; i++)
        for (DWORD k = 0; k < i; k++)
            if (handles[i] == handles[k])
                RT_WIN_STOP("rt_msg_wait", "handle %p twice in the array (the page forbids it; what Windows answers is not verified)",
                            (void *)handles[i]);
    RtMsgQueue *q = queue_of(self);
    rt_dispatcher_lock();
    bool again = q->told && q->fresh;
    rt_dispatcher_unlock();
    if (again)
        RT_WIN_STOP("rt_msg_wait", "a second wait after one that returned for the queue, with no rt_msg_peek or rt_msg_get "
                    "between (whether Windows still counts that message as new is not verified)");

    RtObject *objs[MAXIMUM_WAIT_OBJECTS];
    for (DWORD i = 0; i < n; i++) {
        objs[i] = rt_handle_ref(handles[i], RT_K_ANY);
        if (!objs[i]) {
            while (i > 0) rt_object_unref(objs[--i]);
            rt_set_last_error(ctx, ERROR_INVALID_HANDLE);
            return WAIT_FAILED;
        }
    }
    objs[n] = &q->obj;
    /* With an input script that has events left the wait sleeps in slices and delivers what is
     * due in between; with none this is one rt_wait with `ms`, as before. */
    const uint64_t start = rt_time_tick_ms();
    DWORD r;
    for (;;) {
        rt_win_input_poll();
        const DWORD slice = rt_win_input_slice();
        const uint64_t spent = rt_time_tick_ms() - start;
        const DWORD left = ms == INFINITE ? INFINITE : spent >= ms ? 0 : ms - (DWORD)spent;
        const DWORD t = slice < left ? slice : left;
        r = rt_wait(ctx, n + 1, objs, false, t, false);
        if (r != WAIT_TIMEOUT || t == left) break;
    }
    for (DWORD i = 0; i < n; i++) rt_object_unref(objs[i]);
    if (r == WAIT_OBJECT_0 + n) {
        rt_dispatcher_lock();
        q->told = true;
        rt_dispatcher_unlock();
    }
    return r;
}

LONG rt_msg_time(void)
{
    RtThread *t = rt_thread_self();
    if (!t) rt_fatal("rt_msg_time: called on a host thread that is not a guest thread");
    rt_win_lock();
    RtMsgQueue *q = find_locked(t->id);
    LONG time = q ? q->last_time : 0;
    rt_win_unlock();
    return time;
}
