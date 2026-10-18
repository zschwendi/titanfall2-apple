/* The scripted input source (specification: rt/input.h; contract: docs/handoff/
 * rt-contract-input.md, cited as C<n>; private side: core/win_priv.h).  Built by unit
 * rt-input-script; the keys' scan codes, sides and characters by unit rt-input-keys (rows K<n>;
 * core/win_keymap.c).  Tests: tests/core/input_source.c, input_script.c, input_keys.c.
 *
 * No thread of its own.  rt_win_input_poll (called by the queue's looks and waits and by
 * GetCursorPos) delivers the events that are due, in the order of the file, under `in.lock`.
 * LOCKS: in.lock first, the win lock and the raw-input mutex inside it; in.lock is never
 * taken with the win lock held, and never held across a guest call (delivery only appends to
 * queues; the hit test runs later, on the thread that takes the message).
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win_priv.h"
#include "rt/input.h"
#include "rt/rawinput.h"
#include "rt/thread.h"
#include "rt/time.h"

/* The double-click messages (learn.microsoft.com, WM_LBUTTONDBLCLK: "Double-clicking the left mouse button actually generates a
 * sequence of four messages: WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK, and WM_LBUTTONUP."). */
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDBLCLK 0x0209

enum { EV_MOVETO, EV_MOVE, EV_BUTTON, EV_KEY, EV_ACTIVE };
enum { AN_START, AN_PRESENT, AN_PRESENT_REL, AN_CURSOR, AN_MARK };
static const char *const anchor_name[] = { "start", "present ", "present +", "cursor-shown", "mark " };

typedef struct { uint8_t kind, down, ext; uint16_t section; uint32_t t_ms; int32_t a, b; } Event;
typedef struct { uint8_t anchor; bool fired; uint64_t n, fired_tick, fired_present; char mark[32]; } Section;
enum { HOST_CAP = 4096 };

/* "present 2", "cursor-shown": the anchor as the script writes it. */
static const char *anchor_text(const Section *s, char *buf, size_t n) {
    if (s->anchor == AN_PRESENT || s->anchor == AN_PRESENT_REL) snprintf(buf, n, "%s%llu", anchor_name[s->anchor], (unsigned long long)s->n);
    else if (s->anchor == AN_MARK) snprintf(buf, n, "%s%s", anchor_name[s->anchor], s->mark);
    else snprintf(buf, n, "%s", anchor_name[s->anchor]);
    return buf;
}

static struct {
    _Atomic bool active, pending;       /* a script is loaded; it has events left (or a host event is queued) */
    _Atomic bool host;                  /* a host is attached (rt_input_host_attach) */
    pthread_mutex_t lock;
    char     name[200];
    Event   *ev;
    Section *sec;
    unsigned n_ev, next, n_sec, cur;    /* next: the first event not delivered; cur: the first section not fired */
    uint64_t presents;                  /* the number of the last Present reported */
    unsigned buttons;                   /* MK_ bits of the buttons that are down now */
    uint8_t  keys[256];                 /* down now (the hardware's state, not GetKeyState's) */
    unsigned raw, legacy, dropped, silent;
    _Atomic unsigned taken;             /* input messages a queue's owner has taken (PeekMessage with PM_REMOVE, GetMessage) */
    uint64_t last_down[3];              /* C31: tick, point and window of each button's last press */
    POINT    last_pt[3];
    HWND     last_win[3];
    RtHostInput hq[HOST_CAP];           /* the host's queue: a ring; hq_tick the tick of each push */
    uint64_t hq_tick[HOST_CAP];
    unsigned hq_head, hq_count;
    unsigned hevents, hdelivered;       /* host pushes (all of them), and host events delivered */
} in = { .lock = PTHREAD_MUTEX_INITIALIZER };

bool rt_input_active(void) { return atomic_load_explicit(&in.active, memory_order_relaxed) || atomic_load_explicit(&in.host, memory_order_relaxed); }

/* ---- loading ------------------------------------------------------------------------------------ */

static long number(const char *name, unsigned line, const char *tok, const char *what) {
    char *end = NULL;
    const long v = tok ? strtol(tok, &end, 0) : 0;
    if (!tok || end == tok || *end) rt_fatal("rt_input_script: %s:%u: %s: \"%s\" is not a number", name, line, what, tok ? tok : "(missing)");
    return v;
}

static bool is_word(const char *tok, const char *w) { return tok && !strcmp(tok, w); }

void rt_input_script_text(const char *name, const char *text)
{
    if (atomic_load(&in.active)) rt_fatal("rt_input_script: a second script (%s) while %s is loaded", name, in.name);
    char *copy = strdup(text), *save = NULL;
    unsigned lines = 1;
    for (const char *p = text; *p; p++) lines += *p == '\n';
    in.ev = calloc(lines, sizeof *in.ev);
    in.sec = calloc(lines, sizeof *in.sec);
    if (!copy || !in.ev || !in.sec) rt_fatal("rt_input_script: no host memory for %s", name);
    snprintf(in.name, sizeof in.name, "%s", name);
    unsigned line = 0;
    uint32_t last_t = 0;
    for (char *l = copy, *nl; l; l = nl ? nl + 1 : NULL) {
        nl = strchr(l, '\n');
        if (nl) *nl = 0;
        line++;
        char *hash = strchr(l, '#');
        if (hash) *hash = 0;
        char *tok[6] = { 0 };
        unsigned n = 0;
        for (char *t = strtok_r(l, " \t\r", &save); t; t = strtok_r(NULL, " \t\r", &save)) {
            if (n == 6) rt_fatal("rt_input_script: %s:%u: more than 6 words", name, line);
            tok[n++] = t;
        }
        if (!n) continue;
        if (is_word(tok[0], "anchor")) {
            Section *s = &in.sec[in.n_sec++];
            if (is_word(tok[1], "start") && n == 2) s->anchor = AN_START;
            else if (is_word(tok[1], "cursor-shown") && n == 2) s->anchor = AN_CURSOR;
            else if (is_word(tok[1], "present") && n == 3) {
                s->anchor = tok[2][0] == '+' ? AN_PRESENT_REL : AN_PRESENT;
                const long v = number(name, line, tok[2] + (tok[2][0] == '+'), "the Present's number");
                if (v < 1) rt_fatal("rt_input_script: %s:%u: Present number %ld: the first is 1", name, line, v);
                s->n = (uint64_t)v;
            } else if (is_word(tok[1], "mark") && n == 3) {
                const size_t len = strlen(tok[2]);
                if (!len || len >= sizeof s->mark || strspn(tok[2], "abcdefghijklmnopqrstuvwxyz0123456789-") != len)
                    rt_fatal("rt_input_script: %s:%u: mark \"%s\": a mark's name is 1 to %zu of a-z, 0-9 and `-`", name, line, tok[2], sizeof s->mark - 1);
                s->anchor = AN_MARK;
                memcpy(s->mark, tok[2], len + 1);
            } else rt_fatal("rt_input_script: %s:%u: an anchor is `start`, `present N`, `present +N`, `cursor-shown` or `mark NAME`", name, line);
            last_t = 0;
            continue;
        }
        if (!in.n_sec) rt_fatal("rt_input_script: %s:%u: an event before the first `anchor` line", name, line);
        Event *e = &in.ev[in.n_ev++];
        const long t = number(name, line, tok[0], "the time in milliseconds");
        if (t < 0 || t > 86400000 || (uint32_t)t < last_t)
            rt_fatal("rt_input_script: %s:%u: time %ld: below 0, above a day, or before the line above it", name, line, t);
        e->t_ms = last_t = (uint32_t)t;
        e->section = (uint16_t)(in.n_sec - 1);
        if ((is_word(tok[1], "moveto") || is_word(tok[1], "move")) && n == 4) {
            e->kind = is_word(tok[1], "move") ? EV_MOVE : EV_MOVETO;
            e->a = (int32_t)number(name, line, tok[2], "x");
            e->b = (int32_t)number(name, line, tok[3], "y");
        } else if (is_word(tok[1], "button") && n == 4 && (is_word(tok[3], "down") || is_word(tok[3], "up"))) {
            e->kind = EV_BUTTON;
            e->down = is_word(tok[3], "down");
            if (is_word(tok[2], "left")) e->a = 0; else if (is_word(tok[2], "right")) e->a = 1; else if (is_word(tok[2], "middle")) e->a = 2;
            else rt_fatal("rt_input_script: %s:%u: button \"%s\": left, right and middle are built", name, line, tok[2]);
        } else if (is_word(tok[1], "key") && n >= 4 && (is_word(tok[2], "down") || is_word(tok[2], "up"))) {
            e->kind = EV_KEY;
            e->down = is_word(tok[2], "down");
            e->a = (int32_t)number(name, line, tok[3], "the virtual-key code");
            e->b = n > 4 ? (int32_t)number(name, line, tok[4], "the scan code") : 0;
            e->ext = n > 5;
            if (e->a < 1 || e->a > 254 || e->b < 0 || e->b > 255 || (n > 5 && !is_word(tok[5], "ext")))
                rt_fatal("rt_input_script: %s:%u: a key is `key down|up VK [SCAN [ext]]`, VK 1 to 254, SCAN 0 to 255", name, line);
            if (e->a >= 0xA0 && e->a <= 0xA5)                           /* K39 */
                rt_fatal("rt_input_script: %s:%u: virtual key 0x%x names one side of Shift, Control or ALT, which no key message carries: "
                         "write 0x10, 0x11 or 0x12, and the right key by its scan code (0x10 0x36) or `ext` (0x11 0x1d ext)", name, line,
                         (unsigned)e->a);
            if (n == 4) {                                               /* K38: the table's scan code and extended flag */
                UINT scan = 0;
                bool ext = false;
                if (!rt_key_scan((UINT)e->a, &scan, &ext))
                    rt_fatal("rt_input_script: %s:%u: virtual key 0x%x is not in this runtime's US table: write its scan code "
                             "(`key down|up VK SCAN [ext]`)", name, line, (unsigned)e->a);
                e->b = (int32_t)scan;
                e->ext = ext;
            }
        } else if (is_word(tok[1], "active") && n == 3 && (is_word(tok[2], "off") || is_word(tok[2], "on"))) {
            e->kind = EV_ACTIVE;                                        /* the host window stops or starts being the key window */
            e->down = is_word(tok[2], "on");
        } else rt_fatal("rt_input_script: %s:%u: \"%s\" is not `moveto X Y`, `move DX DY`, `button left|right|middle down|up` "
                        "or `key down|up VK [SCAN [ext]]` or `active off|on`", name, line, tok[1] ? tok[1] : "");
    }
    free(copy);
    if (!in.n_sec) rt_fatal("rt_input_script: %s: no `anchor` line", name);
    pthread_mutex_lock(&in.lock);
    if (in.sec[0].anchor == AN_START) { in.sec[0].fired = true; in.sec[0].fired_tick = rt_time_tick_ms(); in.cur = 1; }
    pthread_mutex_unlock(&in.lock);
    atomic_store(&in.pending, in.n_ev > 0);
    atomic_store(&in.active, true);
    rt_log(RT_CH_WINDOW, RT_LL_WARN, "input script: %s loaded: %u event(s) in %u section(s); a mouse and a keyboard exist from here on",
           in.name, in.n_ev, in.n_sec);
}

void rt_input_script_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) rt_fatal("rt_input_script: cannot read %s", path);
    static char text[1 << 20];
    const size_t n = fread(text, 1, sizeof text - 1, f);
    if (!feof(f)) rt_fatal("rt_input_script: %s is larger than %zu bytes", path, sizeof text - 1);
    fclose(f);
    text[n] = 0;
    rt_input_script_text(path, text);
}

/* ---- anchors ------------------------------------------------------------------------------------ */

/* Fire every anchor that can fire now (in.lock held).  `cursor`: a ShowCursor just made the
 * pointer visible.  `mark`: the driver just reported the mark of this name (NULL: none).  Each
 * is a moment, not a state: it fires the section that waits for it now, once, and is gone. */
static void advance_locked(bool cursor, const char *mark)
{
    while (in.cur < in.n_sec) {
        Section *s = &in.sec[in.cur];
        const uint64_t base = in.cur ? in.sec[in.cur - 1].fired_present : 0;
        if (s->anchor == AN_PRESENT && in.presents < s->n) return;
        if (s->anchor == AN_PRESENT_REL && in.presents < base + s->n) return;
        if (s->anchor == AN_CURSOR && !cursor) return;
        if (s->anchor == AN_MARK && !(mark && !strcmp(mark, s->mark))) return;
        cursor = false;
        mark = NULL;
        s->fired = true;
        s->fired_tick = rt_time_tick_ms();
        s->fired_present = in.presents;
        in.cur++;
        char a[40];
        rt_log(RT_CH_WINDOW, RT_LL_WARN, "input script: the anchor of section %u (%s) fired at Present %llu", in.cur,
               anchor_text(s, a, sizeof a), (unsigned long long)in.presents);
    }
}

void rt_input_present(uint64_t nth)
{
    if (!rt_input_active()) return;
    pthread_mutex_lock(&in.lock);
    if (nth > in.presents) in.presents = nth;
    advance_locked(false, NULL);
    pthread_mutex_unlock(&in.lock);
    rt_win_input_poll();
}

void rt_win_input_cursor_shown(void)
{
    if (!rt_input_active()) return;
    pthread_mutex_lock(&in.lock);
    advance_locked(true, NULL);
    pthread_mutex_unlock(&in.lock);
}

void rt_input_mark(const char *name)
{
    if (!rt_input_active() || !name) return;
    pthread_mutex_lock(&in.lock);
    advance_locked(false, name);
    pthread_mutex_unlock(&in.lock);
}

/* ---- delivery ----------------------------------------------------------------------------------- */

static bool no_legacy(USHORT usage) {
    RAWINPUTDEVICE d;
    return rt_raw_input_find(1, usage, &d, NULL) && (d.dwFlags & RIDEV_NOLEGACY) == RIDEV_NOLEGACY;     /* C14 */
}

/* WM_INPUT for one packet (C10, C12, C13). */
static void raw_post(USHORT usage, RAWINPUT *p, DWORD tick, POINT pt)
{
    RAWINPUTDEVICE d;
    if (!rt_raw_input_find(1, usage, &d, NULL)) return;
    const HWND target = d.hwndTarget ? d.hwndTarget : rt_window_focus();
    const HWND fg = rt_window_foreground();
    const DWORD t_thread = target ? rt_window_thread(target, NULL) : 0;
    if (!t_thread) return;
    WPARAM wp;
    if (fg && (fg == target || rt_window_thread(fg, NULL) == t_thread)) wp = RIM_INPUT;
    else if (d.dwFlags & RIDEV_INPUTSINK) wp = RIM_INPUTSINK;
    else return;
    p->header.wParam = wp;
    const HRAWINPUT h = rt_raw_input_new(p);
    if (rt_win_queue_input(target, WM_INPUT, wp, (LPARAM)(uintptr_t)h, tick, pt, RT_IN_RAW)) in.raw++;
    else rt_raw_input_release(h);
}

static void raw_mouse(DWORD tick, POINT pt, USHORT button_flags, LONG dx, LONG dy)
{
    RAWINPUT p = { .header = { .dwType = RIM_TYPEMOUSE, .dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE), .hDevice = RT_HRAWDEV_MOUSE } };
    p.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;                         /* C6 */
    p.data.mouse.usButtonFlags = button_flags;                          /* C7; ulRawButtons and the rest stay 0, C8 */
    p.data.mouse.lLastX = dx;
    p.data.mouse.lLastY = dy;
    raw_post(2, &p, tick, pt);
}

/* The window a mouse message goes to (C21): the capture, else the shown top-level window
 * under the point.  *cls_style its class style. */
static HWND mouse_window(POINT p, UINT *cls_style)
{
    HWND h = atomic_load(&rt_win.capture);
    bool child = false;
    rt_win_lock();
    for (RtWindow *w = rt_win.top; w; w = w->next) {
        const bool under = (w->style & WS_VISIBLE) && p.x >= w->rect.left && p.x < w->rect.right && p.y >= w->rect.top && p.y < w->rect.bottom;
        if (h ? w->self != h : !under) continue;
        h = w->self;
        *cls_style = w->cls ? w->cls->style : 0;
        child = w->first_child != NULL;
        break;
    }
    rt_win_unlock();
    if (h && child) rt_fatal("rt_input_script: not implemented: a mouse event over window %p, which has child windows (which of them is "
                             "under the pointer is not built)", (void *)h);
    return h;
}

static void legacy_mouse(DWORD tick, POINT pt, UINT msg, int button)
{
    if (no_legacy(2)) return;
    UINT cls_style = 0;
    const HWND w = mouse_window(pt, &cls_style);
    if (!w || !rt_window_thread(w, NULL)) { in.dropped++; return; }
    if (!atomic_load(&rt_win.capture)) {                                /* a non-client point is dropped, not delivered */
        RECT c;
        POINT cp = pt;
        /* A point on the title bar or the frame of the window under it: the non-client mouse messages (WM_NCMOUSEMOVE,
         * WM_NCLBUTTONDOWN, dragging a window by its title bar) are not built; the host window moves itself. */
        if (rt_window_get_client_rect(w, &c) && rt_window_screen_to_client(w, &cp)
            && (cp.x < c.left || cp.x >= c.right || cp.y < c.top || cp.y >= c.bottom)) { in.dropped++; return; }
    }
    if (button >= 0) {                                                  /* a press: C31 */
        static const UINT dbl[3] = { WM_LBUTTONDBLCLK, WM_RBUTTONDBLCLK, WM_MBUTTONDBLCLK };
        /* After a double click the count starts over: the third quick press is a plain press and the first of a new pair, so
         * four quick presses are down, double click, down, double click (Windows resets its click count at a double click). */
        const bool again = in.last_win[button] == w && (uint64_t)tick - in.last_down[button] <= 500
                           && labs((long)(pt.x - in.last_pt[button].x)) <= 2 && labs((long)(pt.y - in.last_pt[button].y)) <= 2;
        if (again && (cls_style & 0x0008u /* CS_DBLCLKS */)) {
            msg = dbl[button];                                          /* the double-click message in place of the second press */
            in.last_win[button] = NULL;
        } else {
            in.last_win[button] = w;
            in.last_down[button] = tick;
            in.last_pt[button] = pt;
        }
    }
    POINT c = pt;
    rt_window_screen_to_client(w, &c);
    const WPARAM wp = in.buttons | (in.keys[0x10] ? MK_SHIFT : 0u) | (in.keys[0x11] ? MK_CONTROL : 0u);              /* C22 */
    const LPARAM lp = (LPARAM)(uint32_t)(((uint32_t)(uint16_t)c.y << 16) | (uint16_t)c.x);
    if (rt_win_queue_input(w, msg, wp, lp, tick, pt, RT_IN_MOUSE)) in.legacy++; else in.dropped++;
}

static void deliver(const Event *e, DWORD tick)
{
    POINT pos;
    rt_win_cursor_peek(&pos);
    switch (e->kind) {
    case EV_MOVE: case EV_MOVETO: {
        const LONG dx = e->kind == EV_MOVE ? e->a : e->a - pos.x, dy = e->kind == EV_MOVE ? e->b : e->b - pos.y;
        if (!dx && !dy) { in.silent++; return; }                        /* the pointer is there already: no event */
        rt_win_cursor_move(pos.x + dx, pos.y + dy, &pos);               /* C36 */
        raw_mouse(tick, pos, 0, dx, dy);
        legacy_mouse(tick, pos, WM_MOUSEMOVE, -1);
        return;
    }
    case EV_BUTTON: {
        static const struct { unsigned mk; USHORT down, up; UINT msg; } B[3] = {
            { MK_LBUTTON, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, WM_LBUTTONDOWN },
            { MK_RBUTTON, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, WM_RBUTTONDOWN },
            { MK_MBUTTON, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, WM_MBUTTONDOWN } };
        if (e->down) in.buttons |= B[e->a].mk; else in.buttons &= ~B[e->a].mk;
        raw_mouse(tick, pos, e->down ? B[e->a].down : B[e->a].up, 0, 0);
        legacy_mouse(tick, pos, B[e->a].msg + (e->down ? 0 : 1), e->down ? e->a : -1);                  /* C23 */
        return;
    }
    case EV_ACTIVE:                                                     /* no message, no window: the flag only (rt_win_host_sync acts) */
        atomic_store(&rt_win.host_away, !e->down);
        return;
    default: {
        const unsigned vk = (unsigned)e->a, scan = (unsigned)e->b;
        if (vk == 0x12 || vk == 0xA4 || vk == 0xA5 || vk == 0x79 || in.keys[0x12])                      /* C29 */
            rt_fatal("rt_input_script: not implemented: key 0x%x %s: ALT, F10 and a key while ALT is down are system keys, whose "
                     "messages are not built", vk, e->down ? "down" : "up");
        const bool was = in.keys[vk];
        in.keys[vk] = e->down;
        RAWINPUT p = { .header = { .dwType = RIM_TYPEKEYBOARD, .dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD), .hDevice = RT_HRAWDEV_KEYBOARD } };
        p.data.keyboard = (RAWKEYBOARD){ .MakeCode = (USHORT)scan, .Flags = (USHORT)((e->down ? RI_KEY_MAKE : RI_KEY_BREAK) | (e->ext ? RI_KEY_E0 : 0)),
                                         .VKey = (USHORT)vk, .Message = e->down ? WM_KEYDOWN : WM_KEYUP };    /* C9 */
        raw_post(6, &p, tick, pos);
        if (no_legacy(6)) return;
        const HWND f = rt_window_focus();                               /* C27 */
        const LPARAM lp = (LPARAM)(uint32_t)(1u | scan << 16 | (e->ext ? 1u << 24 : 0) | (e->down ? (was ? 1u << 30 : 0) : 0xC0000000u));  /* C27, C28 */
        if (f && rt_win_queue_input(f, e->down ? WM_KEYDOWN : WM_KEYUP, vk, lp, tick, pos, RT_IN_KEY)) in.legacy++; else in.dropped++;
        return;
    }
    }
}

/* A host event as the script's own Event, with the checks the script loader makes (in.lock held). */
static void host_event(const RtHostInput *h, Event *e)
{
    memset(e, 0, sizeof *e);
    e->down = h->down != 0;
    e->ext = h->ext != 0;
    switch (h->kind) {
    case RT_HI_MOVETO: case RT_HI_MOVE:
        e->kind = h->kind == RT_HI_MOVE ? EV_MOVE : EV_MOVETO;
        e->a = h->a;
        e->b = h->b;
        return;
    case RT_HI_ACTIVE:
        e->kind = EV_ACTIVE;
        return;
    case RT_HI_BUTTON:
        if (h->a < 0 || h->a > 2) rt_fatal("rt_input_host: button %d: 0 left, 1 right and 2 middle are built", (int)h->a);
        e->kind = EV_BUTTON;
        e->a = h->a;
        return;
    case RT_HI_KEY:
        if (h->a < 1 || h->a > 254 || h->b < 0 || h->b > 255)
            rt_fatal("rt_input_host: a key is virtual key 1 to 254 and scan code 0 to 255: virtual key %d, scan code %d", (int)h->a, (int)h->b);
        if (h->a >= 0xA0 && h->a <= 0xA5)                               /* K39 */
            rt_fatal("rt_input_host: virtual key 0x%x names one side of Shift, Control or ALT, which no key message carries: "
                     "push 0x10, 0x11 or 0x12, and the right key by its scan code (0x10 0x36) or ext (0x11 0x1d ext)", (unsigned)h->a);
        e->kind = EV_KEY;
        e->a = h->a;
        e->b = h->b;
        if (!h->b) {                                                    /* K38: the table's scan code and extended flag */
            UINT scan = 0;
            bool ext = false;
            if (!rt_key_scan((UINT)h->a, &scan, &ext))
                rt_fatal("rt_input_host: virtual key 0x%x is not in this runtime's US table: push its scan code (b)", (unsigned)h->a);
            e->b = (int32_t)scan;
            e->ext = ext;
        }
        return;
    default:
        rt_fatal("rt_input_host: event kind %u is not one of MOVETO, MOVE, BUTTON, KEY, ACTIVE", (unsigned)h->kind);
    }
}

void rt_input_host_attach(void)
{
    atomic_store(&in.host, true);
}

bool rt_input_host_push(const RtHostInput *e)
{
    bool ok = false;
    pthread_mutex_lock(&in.lock);
    in.hevents++;
    if (e && atomic_load(&in.host) && in.hq_count < HOST_CAP) {
        const unsigned slot = (in.hq_head + in.hq_count) % HOST_CAP;
        in.hq[slot] = *e;
        in.hq_tick[slot] = rt_time_tick_ms();
        in.hq_count++;
        atomic_store(&in.pending, true);
        ok = true;
    } else in.dropped++;
    pthread_mutex_unlock(&in.lock);
    return ok;
}

void rt_win_input_poll(void)
{
    if (!atomic_load_explicit(&in.pending, memory_order_relaxed)) return;
    pthread_mutex_lock(&in.lock);
    const uint64_t now = rt_time_tick_ms();
    while (in.next < in.n_ev) {
        const Event *e = &in.ev[in.next];
        const Section *s = &in.sec[e->section];
        if (!s->fired || now < s->fired_tick + e->t_ms) break;
        in.next++;
        deliver(e, (DWORD)(s->fired_tick + e->t_ms));                   /* C37: the event's own time, not the poll's */
    }
    while (in.hq_count) {                                               /* the host's events, after the script's due ones */
        Event e;
        const DWORD tick = (DWORD)in.hq_tick[in.hq_head];
        host_event(&in.hq[in.hq_head], &e);
        in.hq_head = (in.hq_head + 1) % HOST_CAP;
        in.hq_count--;
        in.hdelivered++;
        deliver(&e, tick);
    }
    if (in.next == in.n_ev && !in.hq_count) atomic_store(&in.pending, false);
    pthread_mutex_unlock(&in.lock);
}

DWORD rt_win_input_slice(void) { return atomic_load_explicit(&in.pending, memory_order_relaxed) || atomic_load_explicit(&in.host, memory_order_relaxed) ? 10 : INFINITE; }

/* ---- what the owner's look does with an input message ---------------------------------------------- */

/* GetKeyState's table for a key message that was taken (C35, K17).  Shift and Control arrive
 * as the common code; the state has the side's entry too, the right Shift by its scan code and
 * the right Control by the extended-key flag, and the common entry is down while either side
 * is (K18, K19). */
static void key_taken(const MSG *m)
{
    const unsigned vk = (unsigned)m->wParam & 0xFF, scan = (unsigned)(m->lParam >> 16) & 0xFF;
    const bool down = m->message == WM_KEYDOWN, first = down && !(m->lParam & (1 << 30)), ext = (m->lParam >> 24 & 1) != 0;
    const unsigned side = vk == 0x10 ? (scan == 0x36 ? 0xA1u : 0xA0u) : vk == 0x11 ? (ext ? 0xA3u : 0xA2u) : 0;
    if (!side) { rt_win_keystate_note(vk, down, first); return; }
    rt_win_keystate_note(side, down, first);
    rt_win_keystate_note(vk, down || (rt_win_keystate_peek(side ^ 1) & 0x80), first);
}

void rt_win_input_taken(Ctx *ctx, const MSG *m, unsigned kind)
{
    atomic_fetch_add_explicit(&in.taken, 1, memory_order_relaxed);
    if (kind == RT_IN_MOUSE && rt_window_thread(m->hwnd, NULL)) {       /* C24 */
        const LPARAM at = (LPARAM)(uint32_t)(((uint32_t)(uint16_t)m->pt.y << 16) | (uint16_t)m->pt.x);
        const LRESULT hit = rt_window_send(ctx, m->hwnd, WM_NCHITTEST, 0, at, false);
        if (rt_window_thread(m->hwnd, NULL)) {                          /* the procedure may have destroyed the window */
            if (hit != HTCLIENT)
                rt_fatal("rt_msg_peek: not implemented: window %p answered WM_NCHITTEST for the screen point (%d, %d) with %lld; only "
                         "HTCLIENT (1), a point of the client area, is built", (void *)m->hwnd, (int)m->pt.x, (int)m->pt.y, (long long)hit);
            if (!atomic_load(&rt_win.capture))
                rt_window_send(ctx, m->hwnd, WM_SETCURSOR, (WPARAM)(uintptr_t)m->hwnd, (LPARAM)(HTCLIENT | m->message << 16), false);
        }
    }
    switch (m->message) {                                               /* C35 */
    case WM_KEYDOWN: case WM_KEYUP: key_taken(m); break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: rt_win_keystate_note(1, true, true); break;
    case WM_LBUTTONUP:   rt_win_keystate_note(1, false, false); break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: rt_win_keystate_note(2, true, true); break;
    case WM_RBUTTONUP:   rt_win_keystate_note(2, false, false); break;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: rt_win_keystate_note(4, true, true); break;
    case WM_MBUTTONUP:   rt_win_keystate_note(4, false, false); break;
    default: break;
    }
}

bool rt_win_input_def_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT *r)
{
    if (!rt_input_active()) return false;
    *r = 0;
    switch (msg) {
    case WM_INPUT:                                                      /* C11 */
        rt_raw_input_release((HRAWINPUT)(uintptr_t)lp);
        return true;
    case WM_NCHITTEST: {                                                /* C25 */
        POINT p = { (short)(lp & 0xFFFF), (short)(lp >> 16 & 0xFFFF) };
        RECT c;
        if (!rt_window_get_client_rect(h, &c) || !rt_window_screen_to_client(h, &p)) return false;
        if (p.x < c.left || p.x >= c.right || p.y < c.top || p.y >= c.bottom) return false;     /* the frame: not built, the stop */
        *r = HTCLIENT;
        return true;
    }
    case WM_KEYDOWN: case WM_KEYUP:                                     /* C30 */
        return wp != 0x79;                                              /* F10: the stop */
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_CHAR:
        return true;
    default:
        return false;
    }
}

/* ---- the counts ------------------------------------------------------------------------------------ */

void rt_input_counts(unsigned *events, unsigned *delivered, unsigned *dropped)
{
    pthread_mutex_lock(&in.lock);
    if (events) *events = in.n_ev + in.hevents;
    if (delivered) *delivered = in.next + in.hdelivered;
    if (dropped) *dropped = in.dropped;
    pthread_mutex_unlock(&in.lock);
}

void rt_input_report(void)
{
    if (!atomic_load_explicit(&in.active, memory_order_relaxed)) return;
    static char line[700];              /* static: this may run on a small signal stack; no lock, no allocation */
    char wait[160] = "all delivered";
    if (in.next < in.n_ev) {
        const Section *s = &in.sec[in.ev[in.next].section];
        if (s->fired) snprintf(wait, sizeof wait, "the next is due %u ms after the anchor of section %u, which fired", (unsigned)in.ev[in.next].t_ms,
                               (unsigned)in.ev[in.next].section + 1);
        else {
            char a[40];
            snprintf(wait, sizeof wait, "section %u waits for its anchor `%s`", (unsigned)in.ev[in.next].section + 1, anchor_text(s, a, sizeof a));
        }
    }
    int n = snprintf(line, sizeof line, "input: script %s: %u event(s) in %u section(s); delivered %u (posted: %u WM_INPUT, %u legacy message(s); "
                     "%u of them taken by the window's thread; %u legacy message(s) had no window to go to; %u movement(s) of no distance); "
                     "not yet delivered %u (%s); Presents seen %llu\n", in.name, in.n_ev, in.n_sec, in.next, in.raw, in.legacy,
                     atomic_load_explicit(&in.taken, memory_order_relaxed), in.dropped, in.silent, in.n_ev - in.next, wait,
                     (unsigned long long)in.presents);
    if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
    if (n > 0) rt_log_raw(line, (size_t)n);
}
