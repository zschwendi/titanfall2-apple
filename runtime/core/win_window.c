/* Creating, destroying, positioning, showing, sending, dispatching, DefWindowProc
 * (specification: rt/window.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `win-window` (docs/handoff/rt-win-window.md).  Tests: tests/core/win_window.c,
 * tests/core/win_window_pos.c, tests/core/win_window_show.c.
 *
 * STATE: none of its own; the records and the four atomics of rt_win (core/win_priv.h).
 *
 * GUEST CALLS.  A window procedure is guest code (or one of our own shims, reached through
 * its guest address: the game's class procedure is DefWindowProcW itself).  It is called with
 * GUEST_CALL on the calling thread, with NO lock held, and it may do anything: destroy the
 * window, create others, call back in here.  So every function below reads what it needs
 * under the lock, releases it, calls, and looks the record up again by its handle afterwards.
 */
#include <stdlib.h>
#include <string.h>

#include "win_priv.h"
#include "rt/locale.h"
#include "rt/shim.h"
#include "rt/input.h"

#define MAKELONG_(lo, hi)  ((LPARAM)(uint32_t)(((uint32_t)(lo) & 0xFFFFu) | (((uint32_t)(hi) & 0xFFFFu) << 16)))

/* The mouse messages rt/win/user.h and rt/input.h do not have (values as on Microsoft's pages:
 * inputdev/wm-*, menurc/wm-contextmenu, the page of each message). */
#define WM_CONTEXTMENU      0x007B
#define WM_LBUTTONDBLCLK    0x0203
#define WM_RBUTTONDBLCLK    0x0206
#define WM_MBUTTONDBLCLK    0x0209
#define WM_MOUSEWHEEL       0x020A
#define WM_XBUTTONDOWN      0x020B
#define WM_XBUTTONUP        0x020C
#define WM_XBUTTONDBLCLK    0x020D
#define WM_MOUSEHWHEEL      0x020E
#define WM_CAPTURECHANGED   0x0215
#define WM_MOUSEHOVER       0x02A1
#define WM_MOUSELEAVE       0x02A3

/* ---- small helpers ---------------------------------------------------------------------------- */

/* The record behind `h` with a reference, or NULL; unlike rt_win_window_ref it leaves last
 * error alone (a message sent by the system must not change it). */
static RtWindow *ref_quiet(HWND h)
{
    const DWORD e = rt_thread_get_last_error();
    RtWindow *w = (RtWindow *)rt_htab_ref(rt_win_table(), h, RT_K_WINDOW);
    if (!w) rt_thread_set_last_error(e);                        /* the table sets ERROR_INVALID_HANDLE */
    return w;
}

static bool alive(HWND h)
{
    RtWindow *w = ref_quiet(h);
    if (w) rt_object_unref(&w->obj);
    return w != NULL;
}

static DWORD self_tid(void) { return rt_thread_self()->id; }

/* The guest address the innermost shim returns to: who called CreateWindowEx.  0 outside a shim. */
static uint64_t guest_caller(Ctx *ctx)
{
    RtShimFrame *f = RT_THREAD(ctx)->shim;
    return f ? LD64(f->entry_rsp) : 0;
}

static LRESULT call(Ctx *ctx, WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return (LRESULT)GUEST_CALL(ctx, proc, h, msg, wp, lp);
}

/* Send to the procedure `h` has now; 0 if `h` is no window any more. */
static LRESULT send_own(Ctx *ctx, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    RtWindow *w = ref_quiet(h);
    if (!w) return 0;
    rt_win_lock();
    const WNDPROC proc = w->proc;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    return call(ctx, proc, h, msg, wp, lp);
}

/* What a record's rectangles are now.  `window` and `client` are in the coordinates the
 * record keeps (screen for a top-level window, the parent's client area for a child);
 * `framed` is false when rt_window_frame has no size for the styles, and `client` is then
 * not set. */
typedef struct { RECT window, client; DWORD style, exstyle, tid; bool framed, child, message_only; } Geo;

static void client_of(const RECT *window, DWORD style, DWORD exstyle, RECT *client, bool *framed)
{
    RECT in;
    *framed = rt_window_frame(style, exstyle, FALSE, &in);
    if (!*framed) return;
    *client = (RECT){ window->left + in.left, window->top + in.top, window->right - in.right, window->bottom - in.bottom };
    if (client->right < client->left) client->right = client->left;        /* a window smaller than its frame */
    if (client->bottom < client->top) client->bottom = client->top;        /* has an empty client area        */
}

static bool geo_of(HWND h, Geo *g)
{
    RtWindow *w = ref_quiet(h);
    if (!w) return false;
    rt_win_lock();
    g->window = w->rect; g->style = w->style; g->exstyle = w->exstyle; g->tid = w->thread_id;
    g->child = w->up != NULL; g->message_only = w->message_only;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    client_of(&g->window, g->style, g->exstyle, &g->client, &g->framed);
    return true;
}

/* WM_SIZE and WM_MOVE carry the client area: its size, and its top left corner. */
static void send_size(Ctx *ctx, HWND h, const Geo *g)
{
    send_own(ctx, h, WM_SIZE, SIZE_RESTORED, MAKELONG_(g->client.right - g->client.left, g->client.bottom - g->client.top));
}
static void send_move(Ctx *ctx, HWND h, const Geo *g)
{
    send_own(ctx, h, WM_MOVE, 0, MAKELONG_(g->client.left, g->client.top));
}

/* The messages whose parameters are text, or a character, of the sender's type (from memory
 * of Wine's winproc.c; the control messages of the predefined classes are not listed: no
 * such class exists here). */
static bool carries_text(UINT msg)
{
    switch (msg) {
    case WM_CREATE: case WM_NCCREATE: case WM_SETTEXT: case WM_GETTEXT: case WM_GETTEXTLENGTH:
    case 0x001A /* WM_SETTINGCHANGE */: case 0x001B /* WM_DEVMODECHANGE */:
    case WM_CHAR: case 0x0103 /* WM_DEADCHAR */: case 0x0106 /* WM_SYSCHAR */: case 0x0107 /* WM_SYSDEADCHAR */:
    case 0x0120 /* WM_MENUCHAR */: case 0x002F /* WM_CHARTOITEM */: case 0x0220 /* WM_MDICREATE */:
    case WM_IME_CHAR: case 0x030C /* WM_ASKCBFORMATNAME */:
        return true;
    default:
        return false;
    }
}

static void deactivate(Ctx *ctx, HWND h, const char *fn);
static void hide_record(Ctx *ctx, HWND h, UINT extra);

/* ---- creation --------------------------------------------------------------------------------- */

static bool predefined_class(const WCHAR *name)
{
    static const char *const names[] = { "BUTTON", "COMBOBOX", "EDIT", "LISTBOX", "MDICLIENT", "SCROLLBAR", "STATIC",
                                         "COMBOLBOX", "IME", "MESSAGE", "#32768", "#32769", "#32770", "#32771", "#32772" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *n = names[i];
        size_t k = 0;
        while (n[k] && name[k] && (WCHAR)n[k] == ((name[k] >= u'a' && name[k] <= u'z') ? name[k] - 32 : name[k])) k++;
        if (!n[k] && !name[k]) return true;
    }
    return false;
}

static HWND create_fail(DWORD e) { rt_thread_set_last_error(e); return NULL; }

HWND rt_window_create(Ctx *ctx, const RtWindowCreate *a)
{
    RtClassKey key;
    const RtKeyKind kind = rt_win_text_key(a->cls, &key);
    const bool message_only = a->parent == HWND_MESSAGE;
    const bool child = (a->style & WS_CHILD) != 0;
    HWND parent = (message_only || a->parent == RT_HWND_DESKTOP) ? NULL : a->parent;

    /* What Windows refuses, first. */
    if (kind == RT_KEY_NAME && predefined_class(key.name))
        RT_WIN_STOP("rt_window_create", "a predefined control class (none is registered)");
    if (kind == RT_KEY_ATOM && key.atom < 0xC000)
        RT_WIN_STOP("rt_window_create", "class atom 0x%04x: a system class (none is registered)", (unsigned)key.atom);
    RtWndClass *cls = NULL;
    bool cls_unicode = false;
    ATOM atom = 0;
    if (kind == RT_KEY_NAME || kind == RT_KEY_ATOM) {
        rt_win_lock();
        cls = rt_win_class_lookup(&key);
        if (cls) { cls_unicode = cls->unicode; atom = cls->atom; }
        rt_win_unlock();
    }
    if (!cls) return create_fail(ERROR_CANNOT_FIND_WND_CLASS);
    if (child && !a->parent) return create_fail(ERROR_TLW_WITH_WSCHILD);
    DWORD parent_tid = 0;
    if (parent) {
        RtWindow *p = rt_win_window_ref(parent);
        if (!p) return NULL;                                    /* ERROR_INVALID_WINDOW_HANDLE */
        rt_win_lock();
        parent_tid = p->thread_id;
        rt_win_unlock();
        rt_object_unref(&p->obj);
    }

    /* What is not built, or not settled. */
    if (child && (a->style & WS_POPUP))
        RT_WIN_STOP("rt_window_create", "WS_CHILD together with WS_POPUP (style 0x%x)", a->style);
    if (child && a->parent == RT_HWND_DESKTOP)
        RT_WIN_STOP("rt_window_create", "a WS_CHILD window of the desktop");
    if (message_only)
        RT_WIN_STOP("rt_window_create", "a message-only window (which messages its creation sends is not settled: review N1)");
    if (a->style & WS_VISIBLE)
        RT_WIN_STOP("rt_window_create", "WS_VISIBLE at creation (style 0x%x): a window is shown with ShowWindow", a->style);
    if (a->style & (WS_MINIMIZE | WS_MAXIMIZE))
        RT_WIN_STOP("rt_window_create", "WS_MINIMIZE or WS_MAXIMIZE (style 0x%x)", a->style);
    if (a->x == CW_USEDEFAULT || a->cx == CW_USEDEFAULT)
        RT_WIN_STOP("rt_window_create", "CW_USEDEFAULT as a position or a size");
    if (!child && a->menu)
        RT_WIN_STOP("rt_window_create", "a menu (%p) for a window that is not a child", (void *)a->menu);
    if (parent && parent_tid != self_tid())
        RT_WIN_STOP("rt_window_create", "a parent or owner (%p) that belongs to thread 0x%x", (void *)parent, (unsigned)parent_tid);
    if (cls_unicode != a->unicode)
        RT_WIN_STOP("rt_window_create", "a class registered with the %s function and created with the %s one "
                    "(the text of the creation messages would have to be converted)",
                    cls_unicode ? "W" : "A", a->unicode ? "W" : "A");
    if ((a->style & WS_THICKFRAME) || !(a->style & (WS_POPUP | WS_CHILD)))
        RT_WIN_STOP("rt_window_create", "an overlapped or thick-framed window (style 0x%x): WM_GETMINMAXINFO comes first "
                    "and carries frame sizes this desktop does not have (piece win-frame)", a->style);

    /* The two style bits Windows sets by itself (Wine user32/tests/win.c, test_window_styles;
     * docs/reviews/win-window-sequences.md rows 1.8 and 1.10). */
    DWORD style = a->style, exstyle = a->exstyle;
    if (!child) style |= WS_CLIPSIBLINGS;
    if ((exstyle & WS_EX_DLGMODALFRAME) || (!(exstyle & WS_EX_STATICEDGE) && (style & (WS_DLGFRAME | WS_THICKFRAME))))
        exstyle |= WS_EX_WINDOWEDGE;
    else
        exstyle &= ~WS_EX_WINDOWEDGE;

    const INT cx = a->cx < 0 ? 0 : a->cx, cy = a->cy < 0 ? 0 : a->cy;
    RtWinNew n = { .cls = cls, .style = style, .exstyle = exstyle, .rect = { a->x, a->y, a->x + cx, a->y + cy },
                   .parent = parent, .id = child ? (INT_PTR)a->menu : 0, .instance = a->instance, .title = NULL,
                   .unicode = a->unicode, .thread_id = self_tid(), .creator = guest_caller(ctx) };
    const HWND h = rt_win_window_new(&n);
    if (!h) {
        if (rt_thread_get_last_error() == ERROR_NOT_ENOUGH_MEMORY)
            RT_WIN_STOP("rt_window_create", "more than %d windows", RT_WIN_MAX_WINDOWS);
        return NULL;                                            /* the parent went away meanwhile */
    }
    /* The class may have been unregistered between the lookup and the link: the record
     * counted itself on the slot it was given, so the slot is that class if it still has the atom. */
    rt_win_lock();
    const bool same_class = cls->used && cls->atom == atom;
    rt_win_unlock();
    if (!same_class) { rt_win_window_drop(h); return create_fail(ERROR_CANNOT_FIND_WND_CLASS); }

    /* The CREATESTRUCT of the caller's character type: the two forms have one layout. */
    CREATESTRUCTW cs = { .lpCreateParams = a->param, .hInstance = a->instance, .hMenu = a->menu, .hwndParent = a->parent,
                         .cy = a->cy, .cx = a->cx, .y = a->y, .x = a->x, .style = (LONG)a->style,
                         .lpszName = a->unicode ? a->title.w : (LPCWSTR)(uintptr_t)a->title.a,
                         .lpszClass = a->unicode ? a->cls.w : (LPCWSTR)(uintptr_t)a->cls.a, .dwExStyle = exstyle };

    if (!send_own(ctx, h, WM_NCCREATE, 0, (LPARAM)(uintptr_t)&cs))
        RT_WIN_STOP("rt_window_create", "the procedure refused WM_NCCREATE (CreateWindowEx returns NULL; what Windows sends to the window first is not settled: review N1, row 1.13)");
    if (!alive(h)) RT_WIN_STOP("rt_window_create", "the window was destroyed by its own procedure during WM_NCCREATE");

    RECT r = n.rect, want;
    bool framed;
    client_of(&n.rect, style, exstyle, &want, &framed);
    send_own(ctx, h, WM_NCCALCSIZE, 0, (LPARAM)(uintptr_t)&r);
    if (framed && memcmp(&r, &want, sizeof r) != 0)
        RT_WIN_STOP("rt_window_create", "the procedure answered WM_NCCALCSIZE with a client area of its own "
                    "{%d, %d, %d, %d} (a record's client area is its window less the frame)", r.left, r.top, r.right, r.bottom);

    if (send_own(ctx, h, WM_CREATE, 0, (LPARAM)(uintptr_t)&cs) == -1)
        RT_WIN_STOP("rt_window_create", "the procedure refused WM_CREATE (CreateWindowEx returns NULL; which messages destroy the window first is not settled: review N1, row 1.13)");

    Geo g;
    if (geo_of(h, &g) && g.framed) {                            /* a popup or a child: both, hidden or not (review C2) */
        send_size(ctx, h, &g);
        if (geo_of(h, &g) && g.framed) send_move(ctx, h, &g);
    }
    if (child && !(exstyle & WS_EX_NOPARENTNOTIFY) && alive(h))
        send_own(ctx, parent, WM_PARENTNOTIFY, (WPARAM)(WM_CREATE | (((uint32_t)n.id & 0xFFFFu) << 16)), (LPARAM)(uintptr_t)h);
    if (!alive(h)) RT_WIN_STOP("rt_window_create", "the window was destroyed by its own procedure during its creation");

    if (!child) {
        char who[96], name[256 * 3 + 1], title[200];
        RtWindow *w = ref_quiet(h);
        size_t k = 0;
        title[0] = 0;
        if (w) {
            rt_win_lock();
            for (const WCHAR *p = cls->name; *p && k + 1 < sizeof name; p++) name[k++] = *p < 0x80 ? (char)*p : '?';
            size_t t = 0;
            for (const WCHAR *p = w->text; p && *p && t + 1 < sizeof title; p++) title[t++] = *p < 0x80 ? (char)*p : '?';
            title[t] = 0;
            rt_win_unlock();
            rt_object_unref(&w->obj);
        }
        name[k] = 0;
        rt_log(RT_CH_WINDOW, RT_LL_WARN, "CreateWindowEx: top-level window %p of class \"%s\" titled \"%s\", style 0x%08x, "
               "extended style 0x%08x, at (%d, %d) size %d x %d, created by %s on thread 0x%x",
               (void *)h, name, title, style, exstyle, n.rect.left, n.rect.top, cx, cy,
               rt_module_describe(n.creator, who, sizeof who), (unsigned)n.thread_id);
    }
    return h;
}

/* ---- destruction ------------------------------------------------------------------------------ */

/* Win lock held.  Append the subtree of `w` to `pre` in pre-order and to `post` in post-order. */
static void walk(RtWindow *w, HWND *pre, unsigned *npre, HWND *post, unsigned *npost)
{
    pre[(*npre)++] = w->self;
    for (RtWindow *c = w->first_child; c; c = c->next) walk(c, pre, npre, post, npost);
    post[(*npost)++] = w->self;
}

static void destroy_tree(Ctx *ctx, HWND h)
{
    /* The windows `h` owns go first, each completely. */
    for (;;) {
        HWND owned = NULL;
        rt_win_lock();
        for (RtWindow *t = rt_win.top; t; t = t->next)
            if (t->owner == h) { owned = t->self; break; }
        rt_win_unlock();
        if (!owned) break;
        destroy_tree(ctx, owned);
        if (alive(owned)) rt_fatal("rt_window_destroy: owned window %p survived its destruction", (void *)owned);
    }

    RtWindow *w = ref_quiet(h);
    if (!w) return;
    rt_win_lock();
    unsigned total = rt_win.windows, npre = 0, npost = 0;
    HWND *pre = malloc(2 * (size_t)total * sizeof(HWND)), *post = pre ? pre + total : NULL;
    if (pre && w->self) walk(w, pre, &npre, post, &npost);
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (!pre) rt_fatal("rt_window_destroy: no host memory for %u handles", total);

    for (unsigned i = 0; i < npre; i++) {                       /* WM_DESTROY: a window before its children */
        RtWindow *x = ref_quiet(pre[i]);
        if (!x) continue;
        rt_win_lock();
        x->destroying = true;
        rt_win_unlock();
        rt_object_unref(&x->obj);
        send_own(ctx, pre[i], WM_DESTROY, 0, 0);
    }
    for (unsigned i = 0; i < npost; i++) {                      /* WM_NCDESTROY: the children before the window */
        if (!alive(post[i])) continue;
        send_own(ctx, post[i], WM_NCDESTROY, 0, 0);
        RtWindow *x = ref_quiet(post[i]);
        if (!x) continue;
        rt_win_lock();
        const bool has_children = x->first_child != NULL;
        rt_win_unlock();
        rt_object_unref(&x->obj);
        if (has_children)
            RT_WIN_STOP("rt_window_destroy", "window %p got a new child while it was being destroyed", (void *)post[i]);
        const DWORD e = rt_thread_get_last_error();
        rt_win_window_drop(post[i]);
        rt_thread_set_last_error(e);
    }
    free(pre);
}

BOOL rt_window_destroy(Ctx *ctx, HWND h)
{
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_window_destroy", "the desktop window");
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return FALSE;
    rt_win_lock();
    const DWORD tid = w->thread_id, style = w->style, exstyle = w->exstyle;
    const HWND parent = w->up ? w->parent : NULL;
    const INT_PTR id = w->id;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (tid != self_tid()) { rt_thread_set_last_error(ERROR_ACCESS_DENIED); return FALSE; }
    if (style & WS_VISIBLE) {                                   /* S18: hidden with no WM_SHOWWINDOW, then deactivated */
        if (parent) RT_WIN_STOP("rt_window_destroy", "a child window that is shown (%p)", (void *)h);
        hide_record(ctx, h, SWP_NOACTIVATE);
        deactivate(ctx, h, "rt_window_destroy");
        if (!alive(h)) return TRUE;
    }
    if (parent && !(exstyle & WS_EX_NOPARENTNOTIFY))
        send_own(ctx, parent, WM_PARENTNOTIFY, (WPARAM)(WM_DESTROY | (((uint32_t)id & 0xFFFFu) << 16)), (LPARAM)(uintptr_t)h);
    destroy_tree(ctx, h);
    return TRUE;
}

/* ---- the default procedure -------------------------------------------------------------------- */

/* The title as `unicode` text read from guest memory; NULL and "" are both no title. */
static void title_store(RtWindow *w, const void *text, bool unicode)
{
    WCHAR *t = NULL;
    size_t n = 0;
    if (text && unicode) {
        const WCHAR *s = text;
        while (s[n]) n++;
        if (n && (t = malloc((n + 1) * sizeof(WCHAR)))) memcpy(t, s, (n + 1) * sizeof(WCHAR));
    } else if (text) {
        const char *s = text;
        n = strlen(s);
        if (n && (t = malloc((n + 1) * sizeof(WCHAR)))) {
            if (rt_mb_to_wide(1252, 0, s, (int)n, t, (int)n) != (int)n)
                rt_fatal("rt_window_def_proc: code page 1252 did not convert %zu bytes to as many characters", n);
            t[n] = 0;
        }
    }
    if (n && !t) rt_fatal("rt_window_def_proc: no host memory for a title of %zu characters", n);
    rt_win_lock();
    WCHAR *old = w->text;
    w->text = t;
    rt_win_unlock();
    free(old);
}

/* A copy of the title (the caller frees it) and its length in characters. */
static WCHAR *title_copy(RtWindow *w, size_t *len)
{
    rt_win_lock();
    size_t n = 0;
    while (w->text && w->text[n]) n++;
    WCHAR *t = malloc((n + 1) * sizeof(WCHAR));
    if (t) { if (n) memcpy(t, w->text, n * sizeof(WCHAR)); t[n] = 0; }
    rt_win_unlock();
    if (!t) rt_fatal("rt_window_def_proc: no host memory for a title of %zu characters", n);
    *len = n;
    return t;
}

/* Give the keyboard focus to `h`: WM_KILLFOCUS to the window that had it, WM_SETFOCUS to `h`
 * (the two messages' pages: wParam is the other window). */
static void focus_to(Ctx *ctx, HWND h)
{
    const HWND old = atomic_exchange(&rt_win.focus, h);
    if (old == h) return;
    if (old) {
        Geo g;
        if (geo_of(old, &g) && g.tid != self_tid())
            RT_WIN_STOP("rt_window_def_proc", "the focus leaves a window of another thread (%p)", (void *)old);
        send_own(ctx, old, WM_KILLFOCUS, (WPARAM)(uintptr_t)h, 0);
    }
    send_own(ctx, h, WM_SETFOCUS, (WPARAM)(uintptr_t)old, 0);
}

/* `h`, which is shown, stops being the active window (docs/reviews/win-window-sequences.md
 * S8 step 5, S18 step 4, rows 2.11 and 4.2).  Nothing else is activated: `fn` stops if
 * another top-level window is shown (which one Windows activates is not settled: row 4.9). */
static void deactivate(Ctx *ctx, HWND h, const char *fn)
{
    if (atomic_load(&rt_win.active) != h && atomic_load(&rt_win.focus) != h) return;
    HWND other = NULL;
    rt_win_lock();
    for (RtWindow *t = rt_win.top; t; t = t->next)
        if (t->self != h && (t->style & WS_VISIBLE)) { other = t->self; break; }
    rt_win_unlock();
    if (other)
        rt_fatal("%s: not implemented: the active window %p goes away while another window (%p) is shown: "
                 "which window Windows activates then is not settled", fn, (void *)h, (void *)other);
    if (atomic_load(&rt_win.active) == h) {
        if (!send_own(ctx, h, WM_NCACTIVATE, FALSE, 0) && alive(h))
            rt_fatal("%s: not implemented: the procedure of window %p refused its deactivation (WM_NCACTIVATE answered FALSE)", fn, (void *)h);
        HWND expect = h;
        atomic_compare_exchange_strong(&rt_win.active, &expect, NULL);
        expect = h;
        atomic_compare_exchange_strong(&rt_win.foreground, &expect, NULL);
        send_own(ctx, h, WM_ACTIVATE, WA_INACTIVE, 0);
        send_own(ctx, h, WM_ACTIVATEAPP, FALSE, 0);
    }
    HWND expect = h;
    if (atomic_compare_exchange_strong(&rt_win.focus, &expect, NULL)) send_own(ctx, h, WM_KILLFOCUS, 0, 0);
}

/* Take WS_VISIBLE from the record and send the two position messages of a hide (S8 steps 2
 * and 3; S18 steps 2 and 3 with `extra` SWP_NOACTIVATE). */
static void hide_record(Ctx *ctx, HWND h, UINT extra)
{
    Geo g;
    if (!geo_of(h, &g)) return;
    WINDOWPOS pos = { .hwnd = h, .x = g.window.left, .y = g.window.top, .cx = g.window.right - g.window.left,
                      .cy = g.window.bottom - g.window.top, .flags = SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | extra };
    send_own(ctx, h, WM_WINDOWPOSCHANGING, 0, (LPARAM)(uintptr_t)&pos);
    RtWindow *w = ref_quiet(h);
    if (!w) return;
    rt_win_lock();
    w->style &= ~WS_VISIBLE;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    pos.flags |= SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE;
    send_own(ctx, h, WM_WINDOWPOSCHANGED, 0, (LPARAM)(uintptr_t)&pos);
}

void rt_win_hide_top(Ctx *ctx, HWND h)
{
    send_own(ctx, h, WM_SHOWWINDOW, FALSE, 0);
    hide_record(ctx, h, 0);
    deactivate(ctx, h, "rt_window_show");
}

/* The parent's answer to a message the window does not process, passed up (0 for no parent). */
static LRESULT parent_send(Ctx *ctx, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    RtWindow *w = ref_quiet(h);
    if (!w) return 0;
    rt_win_lock();
    const HWND parent = w->parent;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    return parent ? send_own(ctx, parent, msg, wp, lp) : 0;
}

/* The default processing of the mouse messages (rt_window_def_proc's default branch, after the
 * input messages).  Returns false for the messages it does not build: the caller stops.
 * Sources are Microsoft's pages, quoted at each case. */
static bool mouse_def_proc(Ctx *ctx, HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT *r)
{
    switch (msg) {
    case WM_RBUTTONUP: {
        /* https://learn.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu, Remarks: "DefWindowProc
         * generates the WM_CONTEXTMENU message when it processes the WM_RBUTTONUP or WM_NCRBUTTONUP message".
         * The WM_RBUTTONUP page (inputdev/wm-rbuttonup) says nothing about DefWindowProc: return 0.
         * wParam is the window; lParam is the point in screen coordinates (WM_CONTEXTMENU page, Parameters). */
        POINT p = { (short)(lp & 0xFFFF), (short)(lp >> 16 & 0xFFFF) };
        if (!rt_window_client_to_screen(h, &p)) return false;
        send_own(ctx, h, WM_CONTEXTMENU, (WPARAM)(uintptr_t)h,
                 (LPARAM)(uint32_t)(((uint32_t)(uint16_t)p.x) | ((uint32_t)(uint16_t)p.y << 16)));
        *r = 0;
        return true;
    }
    case WM_CONTEXTMENU:
        /* https://learn.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu, Remarks: "If a window is a child
         * window, DefWindowProc sends the message to the parent. Otherwise, DefWindowProc displays a default
         * shortcut menu if the specified position is in the window's caption." A top-level window: 0 (the brief's
         * rule; the caption menu is not built). */
        *r = parent_send(ctx, h, msg, wp, lp);
        return true;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        /* https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel: "The DefWindowProc function
         * propagates the message to the window's parent." (WM_MOUSEHWHEEL page: the same sentence.) No parent: 0. */
        *r = parent_send(ctx, h, msg, wp, lp);
        return true;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_LBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_MOUSELEAVE: case WM_MOUSEHOVER:
    case WM_CAPTURECHANGED:
        /* The page says nothing about what DefWindowProc does (WM_RBUTTONDOWN, WM_RBUTTONDBLCLK, WM_MBUTTONDOWN,
         * WM_MBUTTONUP, WM_MBUTTONDBLCLK, WM_LBUTTONDBLCLK, WM_MOUSELEAVE, WM_MOUSEHOVER, WM_CAPTURECHANGED: "An
         * application should return zero if it processes this message" / "If an application processes this
         * message, it should return zero."; WM_XBUTTONDOWN and WM_XBUTTONDBLCLK: "...should return TRUE if it
         * processes it", and the sentence that the message is passed to DefWindowProc names no default action).
         * Default: do nothing and return 0. */
        *r = 0;
        return true;
    default:
        /* WM_XBUTTONUP is not here: DefWindowProc generates WM_APPCOMMAND for it
         * (https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-appcommand, Remarks), and the pages give
         * no mapping from XBUTTON1/XBUTTON2 to the command, so it stays a stop. */
        return false;
    }
}

LRESULT rt_window_def_proc(Ctx *ctx, HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode)
{
    RtWindow *w = ref_quiet(h);
    if (!w) return 0;
    rt_win_lock();
    const bool w_unicode = w->unicode;
    const DWORD style = w->style, exstyle = w->exstyle;
    rt_win_unlock();
    if (carries_text(msg) && msg != WM_CREATE && unicode != w_unicode) {
        rt_object_unref(&w->obj);
        RT_WIN_STOP("rt_window_def_proc", "message 0x%04x through DefWindowProc%s for a window of the other character type", msg, unicode ? "W" : "A");
    }
    LRESULT r = 0;
    switch (msg) {
    case WM_NULL: case WM_CREATE: case WM_DESTROY: case WM_NCDESTROY: case WM_MOVE: case WM_SIZE:
    case WM_PARENTNOTIFY: case WM_SHOWWINDOW: case WM_ACTIVATEAPP:
    case WM_SETFOCUS: case WM_KILLFOCUS:
        break;
    case WM_NCACTIVATE:
        r = TRUE;
        break;
    case WM_NCCREATE: {
        const CREATESTRUCTW *cs = (const CREATESTRUCTW *)(uintptr_t)lp;
        if (!cs) { rt_object_unref(&w->obj); RT_WIN_STOP("rt_window_def_proc", "WM_NCCREATE with no CREATESTRUCT"); }
        if (cs->lpszName && (uintptr_t)cs->lpszName < 0x10000) {
            rt_object_unref(&w->obj);
            RT_WIN_STOP("rt_window_def_proc", "WM_NCCREATE with a window name that is not text (%p)", (void *)(uintptr_t)cs->lpszName);
        }
        title_store(w, cs->lpszName, unicode);
        r = 1;
        break;
    }
    case WM_NCCALCSIZE: {
        RECT *rc = (RECT *)(uintptr_t)lp, in;                   /* wParam TRUE: the first RECT of an NCCALCSIZE_PARAMS */
        if (!rc) break;                                         /* 0 and no fault (row 5.3) */
        if (!rt_window_frame(style, exstyle, FALSE, &in)) {
            rt_object_unref(&w->obj);
            RT_WIN_STOP("rt_window_def_proc", "WM_NCCALCSIZE for a frame this desktop has no size for: style 0x%x, extended style 0x%x", style, exstyle);
        }
        rc->left += in.left; rc->top += in.top; rc->right -= in.right; rc->bottom -= in.bottom;
        if (rc->right < rc->left) rc->right = rc->left;
        if (rc->bottom < rc->top) rc->bottom = rc->top;
        break;
    }
    case WM_CLOSE:
        rt_object_unref(&w->obj);
        rt_window_destroy(ctx, h);
        return 0;
    case WM_SETTEXT:
        if (lp && (uintptr_t)lp < 0x10000) { rt_object_unref(&w->obj); RT_WIN_STOP("rt_window_def_proc", "WM_SETTEXT with a pointer that is not text"); }
        title_store(w, (const void *)(uintptr_t)lp, unicode);
        r = 1;
        break;
    case WM_GETTEXTLENGTH: {
        size_t n;
        free(title_copy(w, &n));
        r = (LRESULT)n;
        break;
    }
    case WM_GETTEXT: {
        size_t n;
        if (!wp || !lp) break;                                  /* no room for a terminator: nothing copied */
        WCHAR *t = title_copy(w, &n);
        if (n > (size_t)wp - 1) n = (size_t)wp - 1;
        if (unicode) {
            memcpy((WCHAR *)(uintptr_t)lp, t, n * sizeof(WCHAR));
            ((WCHAR *)(uintptr_t)lp)[n] = 0;
        } else {
            char *out = (char *)(uintptr_t)lp;
            if (n && rt_wide_to_mb(1252, 0, t, (int)n, out, (int)n, NULL, NULL) != (int)n)
                rt_fatal("rt_window_def_proc: a title of %zu characters did not become as many bytes of code page 1252", n);
            out[n] = 0;
        }
        free(t);
        r = (LRESULT)n;
        break;
    }
    case WM_WINDOWPOSCHANGING: {
        const WINDOWPOS *p = (const WINDOWPOS *)(uintptr_t)lp;
        if (p && !(p->flags & SWP_NOSIZE) && ((style & WS_THICKFRAME) || !(style & (WS_POPUP | WS_CHILD)))) {
            rt_object_unref(&w->obj);
            RT_WIN_STOP("rt_window_def_proc", "WM_WINDOWPOSCHANGING that sizes an overlapped or thick-framed window "
                        "(style 0x%x): WM_GETMINMAXINFO carries frame sizes this desktop does not have (piece win-frame)", style);
        }
        break;
    }
    case WM_WINDOWPOSCHANGED: {
        const WINDOWPOS *p = (const WINDOWPOS *)(uintptr_t)lp;
        const UINT flags = p ? p->flags : (SWP_NOCLIENTMOVE | SWP_NOCLIENTSIZE);
        rt_object_unref(&w->obj);
        Geo g;
        if (!(flags & SWP_NOCLIENTMOVE) && geo_of(h, &g) && g.framed) send_move(ctx, h, &g);
        if (!(flags & SWP_NOCLIENTSIZE) && geo_of(h, &g) && g.framed) send_size(ctx, h, &g);
        return 0;
    }
    case WM_ACTIVATE:
        rt_object_unref(&w->obj);
        if ((wp & 0xFFFFu) != WA_INACTIVE && !(wp >> 16 & 0xFFFFu)) focus_to(ctx, h);   /* activated and not minimized */
        return 0;
    default:
        if (rt_win_input_def_proc(h, msg, wp, lp, &r)) break;       /* the input messages, with a script loaded (rt/input.h) */
        if (mouse_def_proc(ctx, h, msg, wp, lp, &r)) break;         /* the mouse messages a person produces */
        if (msg < 0x0400 || msg > 0xFFFF) {
            rt_object_unref(&w->obj);
            RT_WIN_STOP("rt_window_def_proc", "message 0x%04x (wParam 0x%llx, lParam 0x%llx) for window %p: its default processing is not built",
                        msg, (unsigned long long)wp, (unsigned long long)lp, (void *)h);
        }
        break;
    }
    rt_object_unref(&w->obj);
    return r;
}

/* ---- calling, sending, dispatching ------------------------------------------------------------ */

LRESULT rt_window_call_proc(Ctx *ctx, WNDPROC proc, HWND h, UINT msg, WPARAM w, LPARAM l, bool unicode)
{
    (void)unicode;                                              /* a procedure is a plain address: nothing is converted */
    if (!proc) RT_WIN_STOP("rt_window_call_proc", "a NULL procedure (message 0x%04x for window %p)", msg, (void *)h);
    return call(ctx, proc, h, msg, w, l);
}

LRESULT rt_window_send(Ctx *ctx, HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode)
{
    if (h == HWND_BROADCAST) RT_WIN_STOP("rt_window_send", "HWND_BROADCAST (message 0x%04x)", msg);
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_window_send", "the desktop window (message 0x%04x)", msg);
    RtWindow *w = rt_win_window_ref(h);
    if (!w) return 0;
    rt_win_lock();
    const DWORD tid = w->thread_id;
    const bool w_unicode = w->unicode;
    const WNDPROC proc = w->proc;
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (tid != self_tid())
        RT_WIN_STOP("rt_window_send", "message 0x%04x for window %p of another thread (0x%x)", msg, (void *)h, (unsigned)tid);
    if (carries_text(msg) && unicode != w_unicode)
        RT_WIN_STOP("rt_window_send", "message 0x%04x, which carries text, sent with the %s function to a window of the other character type",
                    msg, unicode ? "W" : "A");
    return call(ctx, proc, h, msg, wp, lp);
}

LRESULT rt_window_dispatch(Ctx *ctx, const MSG *msg, bool unicode)
{
    if (!msg) RT_WIN_STOP("rt_window_dispatch", "a NULL message");
    if (!msg->hwnd) return 0;
    if (msg->message == WM_TIMER && msg->lParam)
        RT_WIN_STOP("rt_window_dispatch", "WM_TIMER with a callback (0x%llx)", (unsigned long long)msg->lParam);
    return rt_window_send(ctx, msg->hwnd, msg->message, msg->wParam, msg->lParam, unicode);
}

BOOL rt_window_set_text(Ctx *ctx, HWND h, RtText text)
{
    if (!alive(h)) { rt_thread_set_last_error(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
    const bool unicode = text.a == NULL;
    return rt_window_send(ctx, h, WM_SETTEXT, 0, (LPARAM)(uintptr_t)(unicode ? (const void *)text.w : (const void *)text.a), unicode) != 0;
}

INT rt_window_get_text(Ctx *ctx, HWND h, void *buf, INT cap, bool unicode)
{
    if (!alive(h)) { rt_thread_set_last_error(ERROR_INVALID_WINDOW_HANDLE); return 0; }
    if (cap <= 0 || !buf) return 0;
    return (INT)rt_window_send(ctx, h, WM_GETTEXT, (WPARAM)cap, (LPARAM)(uintptr_t)buf, unicode);
}

/* ---- positioning ------------------------------------------------------------------------------ */

/* Win lock held: take `w` out of its sibling list and put it first, last, or after `prev`. */
static void relink(RtWindow *w, RtWindow *after, bool bottom)
{
    RtWindow **head = w->up ? &w->up->first_child : (w->message_only ? &rt_win.message_only : &rt_win.top);
    if (w->prev) w->prev->next = w->next; else *head = w->next;
    if (w->next) w->next->prev = w->prev;
    if (bottom) { after = *head; while (after && after->next) after = after->next; }
    w->prev = after;
    w->next = after ? after->next : *head;
    if (w->next) w->next->prev = w;
    if (after) after->next = w; else *head = w;
}

/* `h`, a top-level window of the calling thread, becomes the active window of the foreground
 * application (docs/reviews/win-window-sequences.md S5 steps 4 to 7, rows 2.6 and 2.7).  The
 * focus follows from the default processing of WM_ACTIVATE. */
static void activate(Ctx *ctx, HWND h, const char *fn)
{
    const HWND was = atomic_load(&rt_win.active);
    if (was == h) return;
    if (was)
        rt_fatal("%s: not implemented: window %p becomes active while window %p is (the messages that deactivate it are not built)",
                 fn, (void *)h, (void *)was);
    send_own(ctx, h, WM_ACTIVATEAPP, TRUE, 0);
    send_own(ctx, h, WM_NCACTIVATE, TRUE, 0);
    if (!alive(h)) return;
    atomic_store(&rt_win.active, h);
    atomic_store(&rt_win.foreground, h);
    send_own(ctx, h, WM_ACTIVATE, WA_ACTIVE, 0);
}

/* The host window stops being the key window (the host says so: "active off", RT_HI_ACTIVE):
 * the active window `h` of this thread is deactivated, with the messages of deactivate() in
 * the same order (docs/reviews/win-window-sequences.md S8 step 5, S18 step 4), but with no
 * check for another shown window, and the answer to WM_NCACTIVATE ignored: another application
 * taking the foreground cannot be refused.  host_inactive = h, so the coming "on" can undo it. */
static void host_deactivate(Ctx *ctx, HWND h)
{
    send_own(ctx, h, WM_NCACTIVATE, FALSE, 0);
    if (!alive(h)) return;
    HWND expect = h;
    atomic_compare_exchange_strong(&rt_win.active, &expect, NULL);
    expect = h;
    atomic_compare_exchange_strong(&rt_win.foreground, &expect, NULL);
    send_own(ctx, h, WM_ACTIVATE, WA_INACTIVE, 0);
    if (!alive(h)) return;
    send_own(ctx, h, WM_ACTIVATEAPP, FALSE, 0);
    if (!alive(h)) return;
    expect = h;
    if (atomic_compare_exchange_strong(&rt_win.focus, &expect, NULL)) {
        send_own(ctx, h, WM_KILLFOCUS, 0, 0);
        if (!alive(h)) return;
    }
    atomic_store(&rt_win.host_inactive, h);
}

/* The host window is the key window again, and `h` is the window host_deactivate took away:
 * the messages of activate() in the same order (S5 steps 4 to 7), nothing if `h` is gone or
 * another window is active now.  The focus follows from the default WM_ACTIVATE. */
static void host_activate(Ctx *ctx, HWND h)
{
    if (!alive(h) || atomic_load(&rt_win.active)) return;
    send_own(ctx, h, WM_ACTIVATEAPP, TRUE, 0);
    if (!alive(h)) return;
    send_own(ctx, h, WM_NCACTIVATE, TRUE, 0);
    if (!alive(h)) return;
    atomic_store(&rt_win.active, h);
    atomic_store(&rt_win.foreground, h);
    send_own(ctx, h, WM_ACTIVATE, WA_ACTIVE, 0);
}

/* Called by rt_msg_peek after the input poll, on the thread `ctx` belongs to.  The host went
 * away (host_away set): the active window of this thread is deactivated (host_deactivate).  The
 * host is the key window again (host_away clear): if host_inactive is a window of this thread,
 * it is cleared and host_activate runs.  Anything else does nothing, so "off" twice sends one
 * set of messages, and "off" then "on" before the thread pumps sends none.  Windows of other
 * threads are not touched.  Two atomic loads when nothing changed. */
void rt_win_host_sync(Ctx *ctx)
{
    const DWORD tid = RT_THREAD(ctx)->id;
    if (atomic_load(&rt_win.host_away)) {
        const HWND h = atomic_load(&rt_win.active);
        if (h && rt_window_thread(h, NULL) == tid) host_deactivate(ctx, h);
        return;
    }
    const HWND h = atomic_load(&rt_win.host_inactive);
    if (!h) return;
    const DWORD t = rt_window_thread(h, NULL);
    if (t && t != tid) return;                                          /* another thread's window: its own pump */
    atomic_store(&rt_win.host_inactive, NULL);
    host_activate(ctx, h);
}

BOOL rt_window_set_pos(Ctx *ctx, HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags)
{
    const UINT known = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW |
                       SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_DEFERERASE | SWP_ASYNCWINDOWPOS;
    Geo g;
    if (h == RT_HWND_DESKTOP) RT_WIN_STOP("rt_window_set_pos", "the desktop window");
    if (!geo_of(h, &g)) { rt_thread_set_last_error(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
    if (g.tid != self_tid()) RT_WIN_STOP("rt_window_set_pos", "window %p of another thread (0x%x)", (void *)h, (unsigned)g.tid);
    if (flags & ~known) RT_WIN_STOP("rt_window_set_pos", "flags 0x%x: bits Windows does not define", flags);
    if (flags & SWP_SHOWWINDOW) RT_WIN_STOP("rt_window_set_pos", "SWP_SHOWWINDOW (flags 0x%x): a window is shown with ShowWindow", flags);
    if ((flags & SWP_HIDEWINDOW) && (g.style & WS_VISIBLE)) RT_WIN_STOP("rt_window_set_pos", "SWP_HIDEWINDOW for a window that is shown");
    if (!g.framed) RT_WIN_STOP("rt_window_set_pos", "a frame this desktop has no size for: style 0x%x, extended style 0x%x", g.style, g.exstyle);
    if (g.child && !(flags & SWP_NOACTIVATE))
        RT_WIN_STOP("rt_window_set_pos", "a child window without SWP_NOACTIVATE (flags 0x%x): what Windows activates is not settled", flags);
    bool orders = !(flags & SWP_NOZORDER) && !g.message_only;
    if (orders) {
        if (after == HWND_TOPMOST || after == HWND_NOTOPMOST) RT_WIN_STOP("rt_window_set_pos", "HWND_TOPMOST or HWND_NOTOPMOST");
        if (after != HWND_TOP && after != HWND_BOTTOM) {
            RtWindow *a = ref_quiet(after), *w = ref_quiet(h);
            if (!a) { if (w) rt_object_unref(&w->obj); RT_WIN_STOP("rt_window_set_pos", "hWndInsertAfter %p is not a window", (void *)after); }
            rt_win_lock();
            orders = w && a != w && a->up == w->up && a->message_only == w->message_only && a->self;    /* else: it does nothing (row 3.10) */
            rt_win_unlock();
            rt_object_unref(&a->obj);
            if (w) rt_object_unref(&w->obj);
        }
    }

    /* WM_WINDOWPOSCHANGING carries the call as it was made (row 3.1). */
    WINDOWPOS pos = { .hwnd = h, .hwndInsertAfter = after, .x = x, .y = y, .cx = cx, .cy = cy, .flags = flags };
    if (!(flags & SWP_NOSENDCHANGING)) {
        send_own(ctx, h, WM_WINDOWPOSCHANGING, 0, (LPARAM)(uintptr_t)&pos);
        if (!geo_of(h, &g)) return TRUE;                        /* destroyed by its procedure */
        if (pos.flags != flags || pos.hwndInsertAfter != after)
            RT_WIN_STOP("rt_window_set_pos", "the procedure changed the flags or the order in WM_WINDOWPOSCHANGING (flags 0x%x to 0x%x)", flags, pos.flags);
    }
    RECT now = g.window;
    if (!(flags & SWP_NOMOVE)) { now.right += pos.x - now.left; now.bottom += pos.y - now.top; now.left = pos.x; now.top = pos.y; }
    if (!(flags & SWP_NOSIZE)) { now.right = now.left + (pos.cx < 0 ? 0 : pos.cx); now.bottom = now.top + (pos.cy < 0 ? 0 : pos.cy); }
    const bool moves = now.left != g.window.left || now.top != g.window.top;
    const bool sizes = now.right - now.left != g.window.right - g.window.left || now.bottom - now.top != g.window.bottom - g.window.top;
    const bool frame = (flags & SWP_FRAMECHANGED) != 0;
    const bool acts = !(flags & SWP_NOACTIVATE) && !g.message_only && atomic_load(&rt_win.active) != h;
    if (!moves && !sizes && !orders && !frame && !acts) return TRUE;       /* nothing changes: no second message (row 3.8) */

    RECT want;
    bool framed;
    client_of(&now, g.style, g.exstyle, &want, &framed);
    if (sizes || frame) {                                       /* row 3.6 */
        NCCALCSIZE_PARAMS p = { .rgrc = { now, g.window, g.client }, .lppos = &pos };
        send_own(ctx, h, WM_NCCALCSIZE, TRUE, (LPARAM)(uintptr_t)&p);
        if (memcmp(&p.rgrc[0], &want, sizeof want) != 0)
            RT_WIN_STOP("rt_window_set_pos", "the procedure answered WM_NCCALCSIZE with a client area of its own "
                        "{%d, %d, %d, %d}", p.rgrc[0].left, p.rgrc[0].top, p.rgrc[0].right, p.rgrc[0].bottom);
    }

    RtWindow *w = ref_quiet(h), *a = (orders && after != HWND_TOP && after != HWND_BOTTOM) ? ref_quiet(after) : NULL;
    if (!w) { if (a) rt_object_unref(&a->obj); return TRUE; }
    rt_win_lock();
    if (w->self) {
        w->rect = now;
        if (orders && (!a || (a->self && a->up == w->up))) relink(w, a, after == HWND_BOTTOM);
    }
    rt_win_unlock();
    rt_object_unref(&w->obj);
    if (a) rt_object_unref(&a->obj);

    if (acts) activate(ctx, h, "rt_window_set_pos");            /* also for a hidden window (row 3.9) */

    /* WM_WINDOWPOSCHANGED carries the rectangle the window has now and what did not change (rows 3.2, 3.3). */
    if (!moves) pos.flags |= SWP_NOMOVE;
    if (!sizes) pos.flags |= SWP_NOSIZE;
    if (want.left == g.client.left && want.top == g.client.top) pos.flags |= SWP_NOCLIENTMOVE;
    if (want.right - want.left == g.client.right - g.client.left && want.bottom - want.top == g.client.bottom - g.client.top)
        pos.flags |= SWP_NOCLIENTSIZE;
    if (!(g.style & WS_VISIBLE)) pos.flags |= SWP_NOREDRAW;
    pos.x = now.left; pos.y = now.top; pos.cx = now.right - now.left; pos.cy = now.bottom - now.top;
    send_own(ctx, h, WM_WINDOWPOSCHANGED, 0, (LPARAM)(uintptr_t)&pos);
    return TRUE;
}

/* ---- the first show of a top-level window (rt_window_show, core/win_state.c, has checked the case) */

void rt_win_show_top(Ctx *ctx, HWND h)
{
    Geo g;
    send_own(ctx, h, WM_SHOWWINDOW, TRUE, 0);
    if (!geo_of(h, &g)) return;
    WINDOWPOS pos = { .hwnd = h, .hwndInsertAfter = HWND_TOP, .x = g.window.left, .y = g.window.top,
                      .cx = g.window.right - g.window.left, .cy = g.window.bottom - g.window.top,
                      .flags = SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE };
    send_own(ctx, h, WM_WINDOWPOSCHANGING, 0, (LPARAM)(uintptr_t)&pos);

    RtWindow *w = ref_quiet(h);
    if (!w) return;
    rt_win_lock();
    if (w->self) { w->style |= WS_VISIBLE; relink(w, NULL, false); }
    rt_win_unlock();
    rt_object_unref(&w->obj);

    activate(ctx, h, "rt_window_show");
    pos.flags |= SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE;
    send_own(ctx, h, WM_WINDOWPOSCHANGED, 0, (LPARAM)(uintptr_t)&pos);
}

/* ---- for the device (the swap chain asks these of its window) --------------------------------- */

bool rt_window_client_size(HWND h, UINT *width, UINT *height)
{
    Geo g;
    if (!geo_of(h, &g)) return false;
    if (!g.framed) RT_WIN_STOP("rt_window_client_size", "a frame this desktop has no size for: style 0x%x, extended style 0x%x", g.style, g.exstyle);
    *width = (UINT)(g.client.right - g.client.left);
    *height = (UINT)(g.client.bottom - g.client.top);
    return true;
}
