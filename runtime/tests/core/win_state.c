/* Show, paint state, focus and capture of a window record (rt/window.h, core/win_state.c).
 * Expected values: the comments of rt_window_show, rt_window_update, rt_window_invalidate,
 * rt_window_validate, rt_window_set_focus, rt_window_set_active, rt_window_set_foreground,
 * rt_window_set_capture and rt_window_release_capture in rt/window.h. */
#include "rt/test.h"
#include "../../core/win_priv.h"

static RtWndClass *cls(const WCHAR *name, INT wnd_extra) {
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .style = 0x2b, .lpfnWndProc = (WNDPROC)0x140001000ull,
                       .cbClsExtra = 16, .cbWndExtra = wnd_extra, .hInstance = (HINSTANCE)0x140000000ull,
                       .hIcon = (HICON)0x27F00, .hCursor = (HCURSOR)0x17F00, .hbrBackground = (HBRUSH)6,
                       .lpszClassName = name, .hIconSm = (HICON)0x27F01 };
    RtClassKey key;
    if (!rt_wndclass_register(&wc, true) || rt_win_text_key(RT_TEXT_W(name), &key) != RT_KEY_NAME) return NULL;
    rt_win_lock();
    RtWndClass *c = rt_win_class_lookup(&key);
    rt_win_unlock();
    return c;
}
static HWND win(RtWndClass *c, HWND parent, DWORD style, INT_PTR id, const WCHAR *title, INT x, INT y, INT cx, INT cy) {
    RtWinNew a = { .cls = c, .style = style, .rect = { x, y, x + cx, y + cy }, .parent = parent, .id = id,
                   .instance = (HINSTANCE)0x140000000ull, .title = title, .unicode = true,
                   .thread_id = LD32(T_CTX()->gs_base + 0x48), .creator = 0x140002000ull };
    return rt_win_window_new(&a);
}

TEST(core, win_state)
{
    RtWndClass *c = cls(u"StateClass", 0);
    HWND h = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 100, 100);
    HWND h2 __attribute__((unused)) = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 100, 100);
    RECT rc __attribute__((unused)) = { 1, 2, 3, 4 };

    T_PLAN(1, "SW_HIDE on a record: FALSE, last error unchanged, style kept") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_show(h, SW_HIDE), FALSE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        RtWindow *w = rt_win_window_ref(h);
        T_REQUIRE(w != NULL);
        rt_win_lock();
        DWORD style = w->style;
        rt_win_unlock();
        rt_object_unref(&w->obj);
        T_EQ(style, WS_POPUP);
    }

    T_PLAN(2, "no window: show with SW_HIDE and with 5 is FALSE, ERROR_INVALID_WINDOW_HANDLE") {
        HWND gone = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 100, 100);
        T_REQUIRE(gone != NULL);
        T_EQ(rt_win_window_drop(gone), TRUE);
        HWND none[3] = { NULL, (HWND)0x40000FF0, gone };
        for (unsigned i = 0; i < 3; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_show(none[i], SW_HIDE), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_show(none[i], 5), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        }
    }

    T_PLAN(3, "a command that is not built stops (the show itself: tests/core/win_window.c)") {
        T_TRAPS(rt_window_show(h, 3), "only SW_HIDE, SW_SHOWNORMAL, SW_SHOW and SW_SHOWDEFAULT are built");
        T_TRAPS(rt_window_show(h, 6), "only SW_HIDE, SW_SHOWNORMAL, SW_SHOW and SW_SHOWDEFAULT are built");
    }

    T_PLAN(4, "update, invalidate and validate of a record: TRUE, last error unchanged") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_update(h), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_invalidate(h, NULL, TRUE), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_invalidate(h, &rc, FALSE), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_validate(h, NULL), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_validate(h, &rc), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(5, "no record: invalidate and validate TRUE, update FALSE") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_invalidate(NULL, NULL, TRUE), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_validate(NULL, NULL), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_update(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    }

    T_PLAN(6, "no window: update, invalidate and validate FALSE, ERROR_INVALID_WINDOW_HANDLE") {
        HWND gone = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 100, 100);
        T_REQUIRE(gone != NULL);
        T_EQ(rt_win_window_drop(gone), TRUE);
        HWND none[2] = { (HWND)0x40000FF0, gone };
        for (unsigned i = 0; i < 2; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_update(none[i]), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_invalidate(none[i], NULL, TRUE), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_validate(none[i], NULL), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        }
    }

    T_PLAN(7, "focus and active of NULL: NULL, foreground FALSE with ERROR_INVALID_WINDOW_HANDLE") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_set_focus(NULL) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_focus() == NULL);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_set_active(NULL) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_foreground(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    }

    T_PLAN(8, "focus, active and foreground of (HWND)0x40000FF0: NULL, NULL, FALSE, ERROR_INVALID_WINDOW_HANDLE") {
        HWND x = (HWND)0x40000FF0;
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_set_focus(x) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_set_active(x) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_foreground(x), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    }

    T_PLAN(9, "focus, active and foreground of a record stop") {
        T_TRAPS(rt_window_set_focus(h), "activation needs a visible window");
        T_TRAPS(rt_window_set_active(h), "activation needs a visible window");
        T_TRAPS(rt_window_set_foreground(h), "activation needs a visible window");
    }
}

TEST(core, win_state_capture)
{
    RtWndClass *c = cls(u"StateClass", 0);
    HWND h = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 100, 100);
    HWND h2 __attribute__((unused)) = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 100, 100);
    RECT rc __attribute__((unused)) = { 1, 2, 3, 4 };

    T_PLAN(10, "set capture of a record: NULL, the capture is h; again h, the capture still h") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_set_capture(h) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(atomic_load(&rt_win.capture) == h);
        T_TRUE(rt_window_set_capture(h) == h);
        T_TRUE(atomic_load(&rt_win.capture) == h);
    }

    T_PLAN(11, "capture held by h: set capture of h2 stops") {
        T_TRAPS(rt_window_set_capture(h2), "rt_window_set_capture: not implemented");
    }

    T_PLAN(12, "release capture: TRUE and no capture; again TRUE; then h2 takes the capture") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_release_capture(), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(atomic_load(&rt_win.capture) == NULL);
        T_EQ(rt_window_release_capture(), TRUE);
        T_TRUE(rt_window_set_capture(h2) == NULL);
        T_TRUE(atomic_load(&rt_win.capture) == h2);
    }

    T_PLAN(13, "set capture of NULL and of (HWND)0x40000FF0 stops") {
        T_TRAPS(rt_window_set_capture(NULL), "rt_window_set_capture: not implemented");
        T_TRAPS(rt_window_set_capture((HWND)0x40000FF0), "rt_window_set_capture: not implemented");
    }

    /* Expected values: rt/window.h, "A WINDOW OF ANOTHER THREAD" (the recording of the original:
     * NULL at inputsystem+0x85dd, five times; Wine's server: access denied, nothing changed). */
    T_PLAN(14, "set capture of another thread's window: NULL with ERROR_ACCESS_DENIED, and the capture stays as it was") {
        RtWinNew other = { .cls = c, .style = WS_POPUP, .rect = { 0, 0, 100, 100 }, .parent = NULL, .id = 0,
                           .instance = (HINSTANCE)0x140000000ull, .title = NULL, .unicode = true,
                           .thread_id = LD32(T_CTX()->gs_base + 0x48) + 4, .creator = 0x140002000ull };
        HWND foreign = rt_win_window_new(&other);
        T_REQUIRE(foreign != NULL);
        T_TRUE(atomic_load(&rt_win.capture) == h2);                 /* from plan 12 */
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_set_capture(foreign) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_TRUE(atomic_load(&rt_win.capture) == h2);
        T_TRUE(T_LOGGED(RT_CH_WINDOW, "the capture is not changed"));
        T_EQ(rt_window_release_capture(), TRUE);                    /* and with no capture held */
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_set_capture(foreign) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_TRUE(atomic_load(&rt_win.capture) == NULL);
    }
}
