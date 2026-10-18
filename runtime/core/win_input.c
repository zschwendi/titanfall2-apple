/* Cursor and keyboard state (specification: rt/window.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `win-input` (docs/handoff/rt-contract-window.md).  Every function below is
 * declared and specified in rt/window.h.  The state is the file-static `ist`: the position, the
 * clip rectangle, the display counter, the current cursor, and whether the first call has set
 * the start state.  It is read and written only with the win lock held; a stop is taken after
 * the lock is released.  rt_desktop() is read before the lock is taken (it never changes).
 * Tests: tests/core/win_input.c.
 */
#include "win_priv.h"

static struct {
    bool    started;                    /* the first call has set the start state */
    POINT   pos;                        /* always inside `clip` once started */
    RECT    clip;                       /* always inside the screen once started */
    INT     show;                       /* the display counter of ShowCursor */
    HCURSOR cursor;                     /* the current cursor, NULL at start */
} ist;

/* GetKeyState's table (rt/input.h; C35): bit 7 down, bit 0 the toggle.  All 0 until the owner of
 * a queue takes a scripted key or button message.  Win lock. */
static uint8_t keystate[256];

static LONG lmax(LONG a, LONG b) { return a > b ? a : b; }
static LONG lmin(LONG a, LONG b) { return a < b ? a : b; }
static BOOL fail_false(DWORD e) { rt_thread_set_last_error(e); return FALSE; }

/* The start state, set by the first call of any function: the position {960, 540}, the whole
 * screen as the clip.  Win lock held. */
static void start_locked(const RECT *screen)
{
    if (ist.started) return;
    ist.started = true;
    ist.pos.x = 960;
    ist.pos.y = 540;
    ist.clip = *screen;
}

/* The point of `c` nearest to (x, y): each coordinate clamped to left .. right - 1 and
 * top .. bottom - 1 (rt/window.h, rt_cursor_set_pos). */
static POINT nearest_inside(const RECT *c, LONG x, LONG y)
{
    POINT p;
    p.x = x < c->left ? c->left : x > c->right - 1 ? c->right - 1 : x;
    p.y = y < c->top ? c->top : y > c->bottom - 1 ? c->bottom - 1 : y;
    return p;
}

/* NULL, or one of the fourteen system cursors: RT_HCURSOR_SYSTEM of 32512 to 32516, 32642 to
 * 32646 or 32648 to 32651 (the IDC_ values of rt/win/user.h). */
static bool cursor_ok(HCURSOR h)
{
    const uintptr_t v = (uintptr_t)h;
    if (!v) return true;
    if ((v & ~(uintptr_t)0xFFFFu) != 0x10000u) return false;
    const uintptr_t id = v & 0xFFFFu;
    return (id >= 32512 && id <= 32516) || (id >= 32642 && id <= 32646) || (id >= 32648 && id <= 32651);
}

void rt_win_cursor_peek(POINT *out)
{
    const RECT screen = rt_desktop()->monitor;
    rt_win_lock();
    start_locked(&screen);
    *out = ist.pos;
    rt_win_unlock();
}

void rt_win_cursor_move(LONG x, LONG y, POINT *out)
{
    const RECT screen = rt_desktop()->monitor;
    rt_win_lock();
    start_locked(&screen);
    *out = ist.pos = nearest_inside(&ist.clip, x, y);
    rt_win_unlock();
}

void rt_win_keystate_note(unsigned vk, bool down, bool flip)
{
    rt_win_lock();
    keystate[vk & 0xFF] = (uint8_t)((down ? 0x80 : 0) | ((keystate[vk & 0xFF] ^ (flip ? 1 : 0)) & 1));
    rt_win_unlock();
}

BOOL rt_cursor_get_pos(POINT *out)
{
    rt_win_input_poll();                            /* the script's movements that are due have happened (C36) */
    const RECT screen = rt_desktop()->monitor;
    rt_win_lock();
    start_locked(&screen);
    const POINT p = ist.pos;
    rt_win_unlock();
    if (!out) return fail_false(ERROR_NOACCESS);
    *out = p;
    return TRUE;
}

BOOL rt_cursor_set_pos(INT x, INT y)
{
    const RECT screen = rt_desktop()->monitor;
    rt_win_lock();
    start_locked(&screen);
    ist.pos = nearest_inside(&ist.clip, x, y);
    rt_win_unlock();
    return TRUE;
}

BOOL rt_cursor_clip(const RECT *rc)
{
    const RECT screen = rt_desktop()->monitor;
    RECT want = screen;                             /* rc NULL: the whole screen */
    enum { APPLY, INVERTED, EMPTY, OFF_SCREEN } why = APPLY;
    if (rc) {
        if (rc->right < rc->left || rc->bottom < rc->top) why = INVERTED;
        else if (rc->left == rc->right || rc->top == rc->bottom) why = EMPTY;
        else {
            want.left = lmax(rc->left, screen.left);
            want.top = lmax(rc->top, screen.top);
            want.right = lmin(rc->right, screen.right);
            want.bottom = lmin(rc->bottom, screen.bottom);
            if (want.left >= want.right || want.top >= want.bottom) why = OFF_SCREEN;
        }
    }
    rt_win_lock();
    start_locked(&screen);
    if (why == APPLY) {
        ist.clip = want;
        ist.pos = nearest_inside(&want, ist.pos.x, ist.pos.y);
    }
    rt_win_unlock();
    switch (why) {
    case APPLY:    return TRUE;
    case INVERTED: return fail_false(ERROR_ACCESS_DENIED);
    case EMPTY:
        RT_WIN_STOP("rt_cursor_clip", "an empty rectangle {%d, %d, %d, %d}, not inverted",
                    (int)rc->left, (int)rc->top, (int)rc->right, (int)rc->bottom);
    default:
        RT_WIN_STOP("rt_cursor_clip", "a rectangle {%d, %d, %d, %d} with no point on the screen",
                    (int)rc->left, (int)rc->top, (int)rc->right, (int)rc->bottom);
    }
}

BOOL rt_cursor_get_clip(RECT *out)
{
    const RECT screen = rt_desktop()->monitor;
    rt_win_lock();
    start_locked(&screen);
    const RECT c = ist.clip;
    rt_win_unlock();
    if (!out) RT_WIN_STOP("rt_cursor_get_clip", "a NULL RECT to fill in");
    *out = c;
    return TRUE;
}

INT rt_cursor_show(BOOL show)
{
    const RECT screen = rt_desktop()->monitor;
    rt_win_lock();
    start_locked(&screen);
    ist.show += show ? 1 : -1;
    const INT count = ist.show;
    rt_win_unlock();
    if (show && count == 0) rt_win_input_cursor_shown();    /* the script's anchor `cursor-shown` (rt/input.h) */
    return count;
}

HCURSOR rt_cursor_set(HCURSOR h)
{
    const RECT screen = rt_desktop()->monitor;
    const bool ok = cursor_ok(h);
    rt_win_lock();
    start_locked(&screen);
    const HCURSOR prev = ist.cursor;
    if (ok) ist.cursor = h;
    rt_win_unlock();
    if (!ok) RT_WIN_STOP("rt_cursor_set", "a cursor that is neither NULL nor a system cursor (%p)", (void *)h);
    return prev;
}

uint8_t rt_win_keystate_peek(unsigned vk)
{
    rt_win_lock();
    const uint8_t k = keystate[vk & 0xFF];
    rt_win_unlock();
    return k;
}

SHORT rt_key_state(INT vk)
{
    if (vk < 0 || vk > 255) RT_WIN_STOP("rt_key_state", "a virtual key %d outside 0 to 255", (int)vk);
    rt_win_lock();
    const uint8_t k = keystate[vk];                 /* 0 for every key unless a script is loaded (C35) */
    rt_win_unlock();
    return (SHORT)((k & 0x80 ? 0x8000 : 0) | (k & 1));
}
