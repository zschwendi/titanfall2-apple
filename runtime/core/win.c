/* The window core's shared part (specification: rt/window.h; private side: core/win_priv.h).
 * BUILT: the "user" handle table, the lock, the desktop's description and its two lookup
 * tables, the frame of a frameless window, the activation getters, and window records
 * (made, found, dropped: no message is sent here).  Tests: tests/core/win_desktop.c,
 * tests/core/win_table.c.
 *
 * State: `rt_win` (win lock, except its four atomics) and the table, made once.
 */
#include <os/lock.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "win_priv.h"

RtWinState rt_win;

static os_unfair_lock  win_lock = OS_UNFAIR_LOCK_INIT;
static pthread_once_t  table_once = PTHREAD_ONCE_INIT;
static RtHandleTable  *table;

static void table_make(void) {
    static const RtHandleTableConfig cfg = { .name = "user", .capacity = 65536, .min_free = 256,
                                             .gen_lo = 256, .gen_hi = 511 };
    table = rt_htab_create(&cfg);
}

RtHandleTable *rt_win_table(void) {
    pthread_once(&table_once, table_make);
    return table;
}

void rt_win_lock(void)   { os_unfair_lock_lock(&win_lock); }
void rt_win_unlock(void) { os_unfair_lock_unlock(&win_lock); }

/* ---- the desktop ------------------------------------------------------------------------------- */

static const RtDesktop desktop = {
    .width = 1920, .height = 1080, .bits_per_pixel = 32, .refresh_hz = 60, .dpi = 96,
    .monitor = { 0, 0, 1920, 1080 }, .work = { 0, 0, 1920, 1080 }, .caption_height = 22,
};

const RtDesktop *rt_desktop(void) { return &desktop; }

typedef struct { INT index, value; } Pair;

static bool lookup(const Pair *t, size_t n, INT index, INT *value) {
    for (size_t i = 0; i < n; i++)
        if (t[i].index == index) { *value = t[i].value; return true; }
    return false;
}

bool rt_desktop_metric(INT index, INT *value) {
    static const Pair t[] = {
        { SM_CXSCREEN, 1920 }, { SM_CYSCREEN, 1080 }, { SM_CXVIRTUALSCREEN, 1920 }, { SM_CYVIRTUALSCREEN, 1080 },
        { SM_XVIRTUALSCREEN, 0 }, { SM_YVIRTUALSCREEN, 0 }, { SM_CMONITORS, 1 }, { SM_SAMEDISPLAYFORMAT, 1 },
        { SM_REMOTESESSION, 0 }, { SM_CLEANBOOT, 0 }, { SM_SWAPBUTTON, 0 },
        { SM_CXICON, 32 }, { SM_CYICON, 32 }, { SM_CXCURSOR, 32 }, { SM_CYCURSOR, 32 },
        { SM_CXSMICON, 16 }, { SM_CYSMICON, 16 },
    };
    return lookup(t, sizeof t / sizeof t[0], index, value);
}

bool rt_desktop_device_cap(INT index, INT *value) {
    static const Pair t[] = {
        { HORZRES, 1920 }, { DESKTOPHORZRES, 1920 }, { VERTRES, 1080 }, { DESKTOPVERTRES, 1080 },
        { BITSPIXEL, 32 }, { PLANES, 1 }, { LOGPIXELSX, 96 }, { LOGPIXELSY, 96 }, { VREFRESH, 60 },
        { HORZSIZE, 508 }, { VERTSIZE, 286 },
    };
    return lookup(t, sizeof t / sizeof t[0], index, value);
}

/* The algorithm is review N4's (Wine user32/tests/win.c, wine_AdjustWindowRectEx, which
 * test_AWR_flags compares with Windows); rt/window.h says which of its numbers this desktop
 * has and which three cases stay stops for want of theirs. */
bool rt_window_frame(DWORD style, DWORD exstyle, BOOL menu, RECT *insets) {
    if (menu || (style & WS_THICKFRAME)) return false;                              /* menu height, sizing border */
    const bool caption = (style & WS_CAPTION) == WS_CAPTION;
    if (caption && (exstyle & WS_EX_TOOLWINDOW)) return false;                      /* small caption height */
    LONG edge = 0;
    if ((exstyle & (WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME)) == WS_EX_STATICEDGE) edge = 1;
    else if ((exstyle & WS_EX_DLGMODALFRAME) || (style & WS_DLGFRAME)) edge = 2;
    if ((style & (WS_BORDER | WS_DLGFRAME)) || (exstyle & WS_EX_DLGMODALFRAME)) edge++;
    if (exstyle & WS_EX_CLIENTEDGE) edge += 2;                                      /* SM_CXEDGE, SM_CYEDGE */
    *insets = (RECT){ edge, edge + (caption ? desktop.caption_height + 1 : 0), edge, edge };
    return true;
}

HWND rt_window_active(void)     { return atomic_load(&rt_win.active); }
HWND rt_window_focus(void)      { return atomic_load(&rt_win.focus); }
HWND rt_window_foreground(void) { return atomic_load(&rt_win.foreground); }

/* ---- window records ---------------------------------------------------------------------------- */

static void window_destroy(RtObject *obj) {
    free(((RtWindow *)obj)->text);
}

static const RtObjectOps window_ops = { .name = "window", .destroy = window_destroy, .no_close_handle = true };

static BOOL fail(DWORD e) { rt_thread_set_last_error(e); return FALSE; }

RtWindow *rt_win_window_ref(HWND h) {
    RtWindow *w = (RtWindow *)rt_htab_ref(rt_win_table(), h, RT_K_WINDOW);
    if (!w) rt_thread_set_last_error(ERROR_INVALID_WINDOW_HANDLE);
    return w;
}

/* Put `w` first in the list that starts at *head (win lock held). */
static void link_first(RtWindow **head, RtWindow *w) {
    w->prev = NULL;
    w->next = *head;
    if (*head) (*head)->prev = w;
    *head = w;
}

/* Put `w` last in that list (win lock held): a new child goes to the bottom of its siblings
 * (Wine user32/tests/win.c, test_children_zorder; docs/reviews/contracts-check-2.md D2). */
static void link_last(RtWindow **head, RtWindow *w) {
    RtWindow *last = *head;
    while (last && last->next) last = last->next;
    w->next = NULL;
    w->prev = last;
    if (last) last->next = w; else *head = w;
}

static RtWindow **list_of(RtWindow *w) {
    if (w->up) return &w->up->first_child;
    return w->message_only ? &rt_win.message_only : &rt_win.top;
}

HWND rt_win_window_new(const RtWinNew *a) {
    if (!a || !a->cls) rt_fatal("rt_win_window_new: no arguments or no class");
    RtWindow *up = NULL;
    bool message_only = a->parent == HWND_MESSAGE;
    if (a->parent && !message_only) {
        up = rt_win_window_ref(a->parent);
        if (!up) return NULL;
    }
    WCHAR *text = NULL;
    if (a->title && a->title[0]) {
        size_t n = 0;
        while (a->title[n]) n++;
        text = malloc((n + 1) * sizeof(WCHAR));
        if (!text) rt_fatal("rt_win_window_new: no host memory for a title of %zu characters", n);
        memcpy(text, a->title, (n + 1) * sizeof(WCHAR));
    }
    RtWindow *w = (RtWindow *)rt_object_new(RT_K_WINDOW, sizeof *w, &window_ops, NULL, NULL);
    if (!w) { free(text); if (up) rt_object_unref(&up->obj); return NULL; }
    w->cls = a->cls;
    w->proc = a->cls->proc;
    w->unicode = a->unicode;
    w->message_only = message_only;
    w->style = a->style;
    w->exstyle = a->exstyle;
    w->rect = a->rect;
    w->id = a->id;
    w->instance = a->instance;
    w->thread_id = a->thread_id;
    w->creator = a->creator;
    w->text = text;
    bool child = up && (a->style & WS_CHILD);
    if (child) w->parent = a->parent; else if (up) w->owner = a->parent;

    rt_win_lock();
    bool room = rt_win.windows < RT_WIN_MAX_WINDOWS;
    if (room) rt_win.windows++;
    rt_win_unlock();
    rt_object_ref(&w->obj);                         /* ours, while the table takes the first */
    HWND h = room ? rt_htab_new(rt_win_table(), &w->obj) : NULL;
    if (!h) {
        if (room) { rt_win_lock(); rt_win.windows--; rt_win_unlock(); }
        else rt_object_unref(&w->obj);              /* the table never took the first reference */
        rt_object_unref(&w->obj);
        if (up) rt_object_unref(&up->obj);
        rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    rt_win_lock();
    w->self = h;
    w->up = child ? up : NULL;
    a->cls->windows++;
    if (w->up) link_last(list_of(w), w); else link_first(list_of(w), w);
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (up) rt_object_unref(&up->obj);
    return h;
}

BOOL rt_win_window_drop(HWND h) {
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return FALSE;
    rt_win_lock();
    if (!w->self) {                                 /* another thread dropped it first */
        rt_win_unlock();
        rt_object_unref(&w->obj);
        return fail(ERROR_INVALID_WINDOW_HANDLE);
    }
    if (w->first_child) {
        rt_win_unlock();
        rt_fatal("rt_win_window_drop: window %p still has children", (void *)h);
    }
    RtWindow **head = list_of(w);
    if (w->prev) w->prev->next = w->next; else *head = w->next;
    if (w->next) w->next->prev = w->prev;
    w->next = w->prev = w->up = NULL;
    w->self = NULL;
    w->cls->windows--;
    rt_win.windows--;
    HWND expect = h;
    atomic_compare_exchange_strong(&rt_win.active, &expect, NULL);      expect = h;
    atomic_compare_exchange_strong(&rt_win.focus, &expect, NULL);       expect = h;
    atomic_compare_exchange_strong(&rt_win.foreground, &expect, NULL);  expect = h;
    atomic_compare_exchange_strong(&rt_win.capture, &expect, NULL);
    rt_win_unlock();
    BOOL closed = rt_htab_close(rt_win_table(), h, RT_K_WINDOW);
    rt_object_unref(&w->obj);
    return closed ? TRUE : fail(ERROR_INVALID_WINDOW_HANDLE);
}

/* ---- device-context and GDI-object records ------------------------------------------------------ */

static const RtObjectOps dc_ops = { .name = "device context", .no_close_handle = true };
static const RtObjectOps gdiobj_ops = { .name = "gdi object", .no_close_handle = true };

/* Put a new object in the user table; its `self` is set under the lock by the caller's write. */
static HANDLE table_put(RtObject *o) {
    HANDLE h = rt_htab_new(rt_win_table(), o);
    if (!h) { rt_object_unref(o); rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY); }
    return h;
}

HDC rt_win_dc_new(RtDcKind kind, HWND window) {
    RtDc *d = (RtDc *)rt_object_new(RT_K_DC, sizeof *d, &dc_ops, NULL, NULL);
    if (!d) { rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    d->kind = kind;
    d->window = window;
    d->text_color = 0x000000; d->bk_color = 0xFFFFFF; d->bk_mode = OPAQUE; d->text_align = 0; d->stretch_mode = BLACKONWHITE;
    rt_object_ref(&d->obj);                         /* ours, while the table takes the first */
    HDC h = (HDC)table_put(&d->obj);
    if (h) { rt_win_lock(); d->self = h; rt_win_unlock(); }
    rt_object_unref(&d->obj);
    return h;
}

HGDIOBJ rt_win_gdiobj_new(RtGdiType type) {
    RtGdiObj *o = (RtGdiObj *)rt_object_new(RT_K_GDIOBJ, sizeof *o, &gdiobj_ops, NULL, NULL);
    if (!o) { rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    o->type = type;
    rt_object_ref(&o->obj);
    HGDIOBJ h = (HGDIOBJ)table_put(&o->obj);
    if (h) { rt_win_lock(); o->self = h; rt_win_unlock(); }
    rt_object_unref(&o->obj);
    return h;
}

static RtObject *ref_keeping_error(HANDLE h, uint32_t kind) {
    const DWORD e = rt_thread_get_last_error();
    RtObject *o = rt_htab_ref(rt_win_table(), h, kind);
    if (!o) rt_thread_set_last_error(e);            /* the table sets ERROR_INVALID_HANDLE */
    return o;
}

RtDc *rt_win_dc_ref(HDC dc)                { return (RtDc *)ref_keeping_error(dc, RT_K_DC); }
RtGdiObj *rt_win_gdiobj_ref(HGDIOBJ obj)   { return (RtGdiObj *)ref_keeping_error(obj, RT_K_GDIOBJ); }
