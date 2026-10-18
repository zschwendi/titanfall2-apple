/* What a window record is asked (specification: rt/window.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `win-query` (docs/handoff/rt-contract-window.md).  Every function below is
 * declared and specified in rt/window.h.  It keeps no state of its own: the records, the lists
 * and the classes are read and written under the win lock (rt_win_lock, rt_win_unlock).
 * Tests: tests/core/win_query.c.
 */
#include <string.h>

#include "win_priv.h"

static int client_origin(HWND h, LONG *x, LONG *y, DWORD *style, DWORD *exstyle);

/* The rectangle of `h` for GetWindowRect (client false) or GetClientRect (client true).
 * Returns 1 with it in *out; 0 for an invalid handle (last error set by rt_win_window_ref);
 * -1 when the frame of the record is not known: *style and *exstyle then hold its styles.
 * Takes the win lock only to read the record; no lock is held on return. */
static int rect_of(HWND h, RECT *out, bool client, DWORD *style, DWORD *exstyle)
{
    if (h == RT_HWND_DESKTOP) {
        const RECT m = rt_desktop()->monitor;
        *out = m;
        if (client) {
            out->left = 0;
            out->top = 0;
            out->right = m.right - m.left;
            out->bottom = m.bottom - m.top;
        }
        return 1;
    }
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return 0;
    rt_win_lock();
    const RECT r = w->rect;
    const HWND up = w->up ? w->up->self : NULL;
    *style = w->style;
    *exstyle = w->exstyle;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (!client) {
        /* a child's rectangle is relative to its parent's client area: add that area's screen position */
        LONG ox = 0, oy = 0;
        if (up) {
            const int s = client_origin(up, &ox, &oy, style, exstyle);
            if (s != 1) return s;
        }
        out->left = r.left + ox;
        out->top = r.top + oy;
        out->right = r.right + ox;
        out->bottom = r.bottom + oy;
        return 1;
    }
    RECT insets;
    if (!rt_window_frame(*style, *exstyle, FALSE, &insets)) return -1;
    out->left = 0;
    out->top = 0;
    out->right = (r.right - r.left) - insets.left - insets.right;
    out->bottom = (r.bottom - r.top) - insets.top - insets.bottom;
    return 1;
}

BOOL rt_window_get_rect(HWND h, RECT *out)
{
    if (!out) RT_WIN_STOP("rt_window_get_rect", "a NULL RECT");
    RECT r;
    DWORD style = 0, exstyle = 0;
    switch (rect_of(h, &r, false, &style, &exstyle)) {
    case 1:  *out = r; return TRUE;
    case 0:  return FALSE;
    default: RT_WIN_STOP("rt_window_get_rect", "a frame for style 0x%08x exstyle 0x%08x", (unsigned)style, (unsigned)exstyle);
    }
}

BOOL rt_window_get_client_rect(HWND h, RECT *out)
{
    if (!out) RT_WIN_STOP("rt_window_get_client_rect", "a NULL RECT");
    RECT r;
    DWORD style = 0, exstyle = 0;
    switch (rect_of(h, &r, true, &style, &exstyle)) {
    case 1:  *out = r; return TRUE;
    case 0:  return FALSE;
    default: RT_WIN_STOP("rt_window_get_client_rect", "a frame for style 0x%08x exstyle 0x%08x", (unsigned)style, (unsigned)exstyle);
    }
}

/* The screen position of the top left corner of the client area of `h`: the window
 * rectangle's corner plus the frame's left and top, added up the chain of parents, since a
 * child's rectangle is relative to its parent's client area.  Returns 1 with *x and *y; 0 for
 * an invalid handle (last error set by rt_win_window_ref); -1 when a frame in the chain is not
 * known, with its styles in *style and *exstyle.  Each record is read under the lock and the
 * lock is released before its frame is asked and before the next record is looked up. */
static int client_origin(HWND h, LONG *x, LONG *y, DWORD *style, DWORD *exstyle)
{
    LONG ox = 0, oy = 0;
    HWND cur = h;
    for (;;) {
        RtWindow *w = rt_win_window_ref(cur);
        if (!w) return 0;
        rt_win_lock();
        const RECT r = w->rect;
        *style = w->style;
        *exstyle = w->exstyle;
        const HWND up = w->up ? w->up->self : NULL;
        rt_win_unlock();
        rt_object_unref(&w->obj);
        RECT insets;
        if (!rt_window_frame(*style, *exstyle, FALSE, &insets)) return -1;
        ox += r.left + insets.left;
        oy += r.top + insets.top;
        if (!up) break;
        cur = up;
    }
    *x = ox;
    *y = oy;
    return 1;
}

BOOL rt_window_client_to_screen(HWND h, POINT *pt)
{
    if (!pt) RT_WIN_STOP("rt_window_client_to_screen", "a NULL POINT (what Windows does is not settled)");
    if (h == RT_HWND_DESKTOP) return TRUE;
    LONG x = 0, y = 0;
    DWORD style = 0, exstyle = 0;
    switch (client_origin(h, &x, &y, &style, &exstyle)) {
    case 1:  pt->x += x; pt->y += y; return TRUE;
    case 0:  return FALSE;
    default: RT_WIN_STOP("rt_window_client_to_screen", "a frame for style 0x%08x exstyle 0x%08x", (unsigned)style, (unsigned)exstyle);
    }
}

BOOL rt_window_screen_to_client(HWND h, POINT *pt)
{
    if (!pt) RT_WIN_STOP("rt_window_screen_to_client", "a NULL POINT (what Windows does is not settled)");
    if (h == RT_HWND_DESKTOP) return TRUE;
    LONG x = 0, y = 0;
    DWORD style = 0, exstyle = 0;
    switch (client_origin(h, &x, &y, &style, &exstyle)) {
    case 1:  pt->x -= x; pt->y -= y; return TRUE;
    case 0:  return FALSE;
    default: RT_WIN_STOP("rt_window_screen_to_client", "a frame for style 0x%08x exstyle 0x%08x", (unsigned)style, (unsigned)exstyle);
    }
}

#define WS_FRAME_BITS    (WS_BORDER | WS_DLGFRAME | WS_THICKFRAME)
#define WS_EX_FRAME_BITS (WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE)

/* Status of lw_read and lw_write.  LW_OK: done.  The others are a failure with ERROR_INVALID_INDEX
 * (LW_INDEX) or a stop, decided by the caller after the lock is released. */
enum { LW_OK, LW_INDEX, LW_UNICODE, LW_VISIBLE, LW_FRAME, LW_PARENT, LW_HINST };

/* What GetWindowLong returns for `v` at `size`: 4 is the low 32 bits, sign-extended. */
static uint64_t norm(uint64_t v, unsigned size)
{
    return size == 4 ? (uint64_t)(int64_t)(int32_t)(uint32_t)v : v;
}

/* Win lock held.  The value of `index` for `size` (4 or 8) into *v.  Extra bytes are read as
 * little-endian bytes.  LW_INDEX with *err for an index this size does not give. */
static int lw_read(const RtWindow *w, INT index, unsigned size, bool unicode, uint64_t *v, DWORD *err)
{
    if (index >= 0) {
        if ((INT_PTR)index + (INT_PTR)size > w->cls->wnd_extra) { *err = ERROR_INVALID_INDEX; return LW_INDEX; }
        *v = 0;
        memcpy(v, w->extra + index, size);
        return LW_OK;
    }
    switch (index) {
    case GWL_STYLE:     *v = w->style; return LW_OK;
    case GWL_EXSTYLE:   *v = w->exstyle; return LW_OK;
    case GWL_ID:        *v = (uint64_t)(INT_PTR)w->id; return LW_OK;
    case GWLP_USERDATA: *v = (uint64_t)w->userdata; return LW_OK;
    case GWLP_WNDPROC:
    case GWLP_HINSTANCE:
    case GWLP_HWNDPARENT:
        if (size != 8) break;                   /* refused at size 4: ERROR_INVALID_INDEX below */
        if (index == GWLP_WNDPROC) {
            if (w->unicode != unicode) return LW_UNICODE;
            *v = (uint64_t)(uintptr_t)w->proc;
        } else if (index == GWLP_HINSTANCE) {
            *v = (uint64_t)(uintptr_t)w->instance;
        } else {
            *v = (uint64_t)(uintptr_t)(w->parent ? w->parent : w->owner);
        }
        return LW_OK;
    default:
        break;
    }
    *err = ERROR_INVALID_INDEX;
    return LW_INDEX;
}

/* Win lock held.  Stores `value` at `index` after lw_read has checked it; the old value comes
 * back in *old.  Nothing is written when the result is not LW_OK. */
static int lw_write(RtWindow *w, INT index, uint64_t value, unsigned size, bool unicode, uint64_t *old, DWORD *err)
{
    const int st = lw_read(w, index, size, unicode, old, err);
    if (st != LW_OK) return st;
    if (index >= 0) {
        memcpy(w->extra + index, &value, size);
        return LW_OK;
    }
    switch (index) {
    case GWL_STYLE:
        if ((value & WS_VISIBLE) && !(w->style & WS_VISIBLE)) return LW_VISIBLE;
        if (((DWORD)value ^ w->style) & WS_FRAME_BITS) return LW_FRAME;
        w->style = (DWORD)value;
        return LW_OK;
    case GWL_EXSTYLE:
        if (((DWORD)value ^ w->exstyle) & WS_EX_FRAME_BITS) return LW_FRAME;
        w->exstyle = (DWORD)value;
        return LW_OK;
    case GWL_ID:        w->id = (INT_PTR)norm(value, size); return LW_OK;
    case GWLP_USERDATA: w->userdata = (LONG_PTR)norm(value, size); return LW_OK;
    case GWLP_WNDPROC:  if (value) w->proc = (WNDPROC)(uintptr_t)value; return LW_OK;   /* 0 changes nothing */
    case GWLP_HWNDPARENT: return LW_PARENT;
    case GWLP_HINSTANCE:  return LW_HINST;
    default:              return LW_INDEX;      /* not reached: lw_read refused it */
    }
}

LONG_PTR rt_window_get_long(HWND h, INT index, unsigned size, bool unicode)
{
    if (size != 4 && size != 8) RT_WIN_STOP("rt_window_get_long", "a size of %u bytes (index %d)", size, index);
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_window_get_long", "the desktop window (index %d)", index);
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return 0;
    uint64_t v = 0;
    DWORD err = 0;
    rt_win_lock();
    const int st = lw_read(w, index, size, unicode, &v, &err);
    const bool win_unicode = w->unicode;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (st == LW_UNICODE)
        RT_WIN_STOP("rt_window_get_long", "GWLP_WNDPROC of a window of the other character type (window unicode %d, asked %d)",
                    (int)win_unicode, (int)unicode);
    if (st != LW_OK) { rt_thread_set_last_error(err); return 0; }
    return (LONG_PTR)norm(v, size);
}

LONG_PTR rt_window_set_long(HWND h, INT index, LONG_PTR value, unsigned size, bool unicode)
{
    if (size != 4 && size != 8) RT_WIN_STOP("rt_window_set_long", "a size of %u bytes (index %d)", size, index);
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_window_set_long", "the desktop window (index %d)", index);
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return 0;
    uint64_t old = 0;
    DWORD err = 0;
    rt_win_lock();
    const int st = lw_write(w, index, (uint64_t)value, size, unicode, &old, &err);
    rt_win_unlock();
    rt_object_unref(&w->obj);
    switch (st) {
    case LW_OK:      return (LONG_PTR)norm(old, size);
    case LW_INDEX:   rt_thread_set_last_error(err); return 0;
    case LW_UNICODE: RT_WIN_STOP("rt_window_set_long", "GWLP_WNDPROC of a window of the other character type (asked %d)", (int)unicode);
    case LW_VISIBLE: RT_WIN_STOP("rt_window_set_long", "GWL_STYLE 0x%08x (old 0x%08x) would set WS_VISIBLE", (unsigned)value, (unsigned)old);
    case LW_FRAME:   RT_WIN_STOP("rt_window_set_long", "a frame bit of index %d changes (value 0x%08x, old 0x%08x)", index, (unsigned)value, (unsigned)old);
    case LW_PARENT:  RT_WIN_STOP("rt_window_set_long", "GWLP_HWNDPARENT with size 8 (not built)");
    default:         RT_WIN_STOP("rt_window_set_long", "GWLP_HINSTANCE with size 8 (not specified)");
    }
}

/* GetClassLongPtr of the window's class.  The class is one per name and has one character
 * type here, so `unicode` is not compared (the piece names no stop for it). */
ULONG_PTR rt_wndclass_get_long(HWND h, INT index, bool unicode)
{
    (void)unicode;
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_wndclass_get_long", "the desktop window (index %d)", index);
    if (index == GCLP_MENUNAME) RT_WIN_STOP("rt_wndclass_get_long", "GCLP_MENUNAME: menus are not built");
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return 0;
    uint64_t v = 0;
    DWORD err = 0;
    rt_win_lock();
    const RtWndClass *c = w->cls;
    if (index >= 0) {
        if ((INT_PTR)index + 8 > c->cls_extra) err = ERROR_INVALID_INDEX;
        else memcpy(&v, c->extra + index, 8);
    } else {
        switch (index) {
        case GCLP_WNDPROC:      v = (uint64_t)(uintptr_t)c->proc; break;
        case GCL_STYLE:         v = c->style; break;
        case GCL_CBWNDEXTRA:    v = (uint64_t)(INT_PTR)c->wnd_extra; break;
        case GCL_CBCLSEXTRA:    v = (uint64_t)(INT_PTR)c->cls_extra; break;
        case GCW_ATOM:          v = c->atom; break;
        case GCLP_HICON:        v = (uint64_t)(uintptr_t)c->icon; break;
        case GCLP_HICONSM:      v = (uint64_t)(uintptr_t)c->icon_sm; break;
        case GCLP_HCURSOR:      v = (uint64_t)(uintptr_t)c->cursor; break;
        case GCLP_HBRBACKGROUND: v = (uint64_t)(uintptr_t)c->background; break;
        case GCLP_HMODULE:      v = (uint64_t)(uintptr_t)c->instance; break;
        default:                err = ERROR_INVALID_INDEX; break;
        }
    }
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (err) { rt_thread_set_last_error(err); return 0; }
    return (ULONG_PTR)v;
}

/* No record is ever iconic (creation with WS_MINIMIZE stops) and none is ever visible: FALSE for
 * a record, the desktop is iconic never and visible always.  An invalid handle sets the error. */
BOOL rt_window_is_iconic(HWND h)
{
    if (h == RT_HWND_DESKTOP) return FALSE;
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return FALSE;
    rt_object_unref(&w->obj);
    return FALSE;
}

BOOL rt_window_is_visible(HWND h)
{
    if (h == RT_HWND_DESKTOP) return TRUE;
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return FALSE;
    rt_win_lock();
    BOOL visible = !w->message_only;                            /* a message-only window never is */
    for (const RtWindow *x = w; x && visible; x = x->up) visible = (x->style & WS_VISIBLE) != 0;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    return visible;
}

/* Win lock held: the handle that `cmd` names from record `w`, or NULL.  The sibling commands walk
 * the list `w` is in: its parent's children, or the top-level windows.  A message-only window is
 * in no list, so it has no sibling. */
static HWND link_of(const RtWindow *w, UINT cmd)
{
    if (cmd == GW_OWNER) return w->owner;
    if (cmd == GW_CHILD) return w->first_child ? w->first_child->self : NULL;
    if (w->message_only) return NULL;
    const RtWindow *head = w->up ? w->up->first_child : rt_win.top;
    const RtWindow *n = NULL;
    switch (cmd) {
    case GW_HWNDFIRST: n = head; break;
    case GW_HWNDLAST:  n = head; while (n && n->next) n = n->next; break;
    case GW_HWNDNEXT:  n = w->next; break;
    case GW_HWNDPREV:  n = w->prev; break;
    default:           break;
    }
    return n ? n->self : NULL;
}

HWND rt_window_get(HWND h, UINT cmd)
{
    if (cmd > GW_ENABLEDPOPUP) { rt_thread_set_last_error(ERROR_INVALID_PARAMETER); return NULL; }
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return NULL;
    if (cmd == GW_ENABLEDPOPUP) {               /* no record is visible: never a popup to find */
        rt_object_unref(&w->obj);
        return NULL;
    }
    rt_win_lock();
    const HWND r = link_of(w, cmd);
    rt_win_unlock();
    rt_object_unref(&w->obj);
    return r;
}

/* GetTopWindow: the first child of `parent`, or for NULL and the desktop the first top-level window. */
HWND rt_window_top(HWND parent)
{
    if (!parent || parent == RT_HWND_DESKTOP) {
        rt_win_lock();
        const HWND t = rt_win.top ? rt_win.top->self : NULL;
        rt_win_unlock();
        return t;
    }
    return rt_window_get(parent, GW_CHILD);
}

DWORD rt_window_thread(HWND h, DWORD *pid)
{
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_window_thread", "the desktop window (which process owns it is not decided)");
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return 0;
    rt_win_lock();
    const DWORD tid = w->thread_id;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (pid) *pid = LD32(rt_thread_self()->ctx.gs_base + 0x40);
    return tid;
}

/* Exact comparison of a window's title (NULL text: the empty title) with a key's name. */
static bool title_is(const WCHAR *text, const WCHAR *name)
{
    static const WCHAR none[1] = { 0 };
    if (!text) text = none;
    while (*text && *text == *name) { text++; name++; }
    return *text == *name;
}

/* FindWindowA/W: the first top-level window in Z order whose class and title match.  No match
 * leaves the last error alone. */
HWND rt_window_find(RtText cls, RtText title)
{
    RtClassKey ck, tk;
    const RtKeyKind ckind = rt_win_text_key(cls, &ck);
    const RtKeyKind tkind = rt_win_text_key(title, &tk);
    if (tkind == RT_KEY_ATOM || tkind == RT_KEY_LONG)
        RT_WIN_STOP("rt_window_find", "a title that is an atom or has more than 255 characters (not specified)");
    bool any_class = ckind == RT_KEY_NULL;
    bool no_class = ckind == RT_KEY_EMPTY || ckind == RT_KEY_LONG;
    HWND found = NULL;
    rt_win_lock();
    const RtWndClass *want = NULL;
    if (!any_class && !no_class) {
        want = rt_win_class_lookup(&ck);
        no_class = !want;
    }
    for (const RtWindow *w = rt_win.top; w && !found && !no_class; w = w->next)
        if ((any_class || w->cls == want) && (tkind == RT_KEY_NULL || title_is(w->text, tk.name)))
            found = w->self;
    rt_win_unlock();
    return found;
}

/* EnumWindows' list: the top-level windows in Z order, as many as `cap` holds; the count of all. */
unsigned rt_window_list_top(HWND *out, unsigned cap)
{
    if (!out && cap) RT_WIN_STOP("rt_window_list_top", "a NULL buffer for %u handles", cap);
    unsigned n = 0;
    rt_win_lock();
    for (const RtWindow *w = rt_win.top; w; w = w->next, n++)
        if (n < cap) out[n] = w->self;
    rt_win_unlock();
    return n;
}

/* GetDlgItem: the first child of `dlg` in Z order with identifier `id`; its own children are not searched. */
HWND rt_window_child_by_id(HWND dlg, INT id)
{
    RtWindow *w = rt_win_window_ref(dlg);
    if (!w) return NULL;
    HWND found = NULL;
    rt_win_lock();
    for (const RtWindow *c = w->first_child; c && !found; c = c->next)
        if (c->id == id) found = c->self;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (!found) rt_thread_set_last_error(ERROR_CONTROL_ID_NOT_FOUND);
    return found;
}
