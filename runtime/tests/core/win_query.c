/* What a window record is asked (rt_window_get_rect ... rt_window_child_by_id; core/win_query.c,
 * not built yet).  Typed from the numbered plan in swarm/briefs/core-win-query-test.md, case by
 * case, with the exact values of the plan.  Each T_PLAN(N, ...) block is the plan's case N. */
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

/* A window made and removed again: a "no window" handle of the third kind. */
static HWND made_and_dropped(RtWndClass *c) {
    HWND h = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 1, 1);
    T_TRUE(rt_win_window_drop(h));
    return h;
}

static bool rect_is(const RECT *r, LONG l, LONG t, LONG rr, LONG b) {
    return r->left == l && r->top == t && r->right == rr && r->bottom == b;
}

TEST(core, win_query)
{
    RtWndClass *c = cls(u"QueryClass", 16);
    HWND top = win(c, NULL, WS_POPUP, 0, u"Main", 100, 200, 300, 400);
    HWND kid = win(c, top, WS_CHILD, 42, NULL, 10, 20, 30, 40);
    HWND kid2 = win(c, top, WS_CHILD, 43, NULL, 0, 0, 5, 5);
    HWND owned = win(c, top, WS_POPUP, 0, NULL, 0, 0, 50, 50);
    HWND mo = win(cls(u"MsgOnly", 0), HWND_MESSAGE, 0, 0, u"Main", 0, 0, 0, 0);
    DWORD tid = LD32(T_CTX()->gs_base + 0x48);
    (void)kid2; (void)owned; (void)mo; (void)tid;

    T_PLAN(1, "window rectangle") {
        RECT r = { 0, 0, 0, 0 };
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_get_rect(top, &r));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rect_is(&r, 100, 200, 400, 600));
        T_TRUE(rt_window_get_rect(kid, &r));
        T_TRUE(rect_is(&r, 110, 220, 140, 260));
        T_TRUE(rt_window_get_rect(RT_HWND_DESKTOP, &r));
        T_TRUE(rect_is(&r, 0, 0, 1920, 1080));
    }

    T_PLAN(2, "client rectangle") {
        RECT r = { 0, 0, 0, 0 };
        T_TRUE(rt_window_get_client_rect(top, &r));
        T_TRUE(rect_is(&r, 0, 0, 300, 400));
        T_TRUE(rt_window_get_client_rect(kid, &r));
        T_TRUE(rect_is(&r, 0, 0, 30, 40));
        T_TRUE(rt_window_get_client_rect(RT_HWND_DESKTOP, &r));
        T_TRUE(rect_is(&r, 0, 0, 1920, 1080));
    }

    T_PLAN(3, "NULL rectangle stops; no window fails") {
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        T_TRAPS(rt_window_get_rect(top, NULL), "rt_window_get_rect: not implemented");
        T_TRAPS(rt_window_get_client_rect(top, NULL), "rt_window_get_client_rect: not implemented");
        for (unsigned i = 0; i < 3; i++) {
            RECT r = { 7, 7, 7, 7 };
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(!rt_window_get_rect(none[i], &r));
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            T_TRUE(rect_is(&r, 7, 7, 7, 7));
            r = (RECT){ 7, 7, 7, 7 };
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(!rt_window_get_client_rect(none[i], &r));
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            T_TRUE(rect_is(&r, 7, 7, 7, 7));
        }
    }

    T_PLAN(4, "framed windows: a border takes 1 all round; a thick frame's client rectangle stops") {
        HWND flat = win(c, NULL, WS_POPUP | WS_BORDER, 0, NULL, 0, 0, 100, 100);
        RECT r = { 0, 0, 0, 0 };
        T_TRUE(rt_window_get_rect(flat, &r));
        T_TRUE(rect_is(&r, 0, 0, 100, 100));
        T_TRUE(rt_window_get_client_rect(flat, &r));
        T_TRUE(rect_is(&r, 0, 0, 98, 98));
        POINT p = { 0, 0 };
        T_TRUE(rt_window_client_to_screen(flat, &p));
        T_TRUE(p.x == 1 && p.y == 1);
        HWND thick = win(c, NULL, WS_POPUP | WS_THICKFRAME, 0, NULL, 0, 0, 100, 100);
        T_TRAPS(rt_window_get_client_rect(thick, &r), "rt_window_get_client_rect: not implemented");
    }

    T_PLAN(5, "client to screen and back") {
        POINT p = { 5, 6 };
        T_TRUE(rt_window_client_to_screen(top, &p));
        T_TRUE(p.x == 105 && p.y == 206);
        T_TRUE(rt_window_screen_to_client(top, &p));
        T_TRUE(p.x == 5 && p.y == 6);
        p = (POINT){ 1, 2 };
        T_TRUE(rt_window_client_to_screen(kid, &p));
        T_TRUE(p.x == 111 && p.y == 222);
        p = (POINT){ 5, 6 };
        T_TRUE(rt_window_client_to_screen(RT_HWND_DESKTOP, &p));
        T_TRUE(p.x == 5 && p.y == 6);
        T_TRUE(rt_window_screen_to_client(RT_HWND_DESKTOP, &p));
        T_TRUE(p.x == 5 && p.y == 6);
    }

    T_PLAN(6, "no window: FALSE, invalid handle, point unchanged") {
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        for (unsigned i = 0; i < 3; i++) {
            POINT p = { 5, 6 };
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(!rt_window_client_to_screen(none[i], &p));
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            T_TRUE(p.x == 5 && p.y == 6);
            p = (POINT){ 5, 6 };
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(!rt_window_screen_to_client(none[i], &p));
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            T_TRUE(p.x == 5 && p.y == 6);
        }
    }
}

TEST(core, win_query_style)
{
    RtWndClass *c = cls(u"QueryClass", 16);
    HWND top = win(c, NULL, WS_POPUP, 0, u"Main", 100, 200, 300, 400);
    HWND kid = win(c, top, WS_CHILD, 42, NULL, 10, 20, 30, 40);
    HWND kid2 = win(c, top, WS_CHILD, 43, NULL, 0, 0, 5, 5);
    HWND owned = win(c, top, WS_POPUP, 0, NULL, 0, 0, 50, 50);
    HWND mo = win(cls(u"MsgOnly", 0), HWND_MESSAGE, 0, 0, u"Main", 0, 0, 0, 0);
    DWORD tid = LD32(T_CTX()->gs_base + 0x48);
    (void)kid2; (void)mo; (void)tid;

    T_PLAN(7, "style, id, user data, the rest of the records' longs") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_get_long(top, GWL_STYLE, 4, true), (LONG_PTR)(LONG)0x80000000);
        T_EQ((DWORD)rt_window_get_long(top, GWL_STYLE, 8, true), 0x80000000u);
        T_EQ(rt_window_get_long(kid, GWL_STYLE, 4, true), 0x40000000);
        T_EQ(rt_window_get_long(top, GWL_EXSTYLE, 4, true), 0);
        T_EQ(rt_window_get_long(kid, GWL_ID, 4, true), 42);
        T_EQ(rt_window_get_long(top, GWLP_USERDATA, 4, true), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(8, "instance, procedure, parent, size 8, unicode") {
        T_EQ(rt_window_get_long(top, GWLP_HINSTANCE, 8, true), 0x140000000ull);
        T_EQ(rt_window_get_long(top, GWLP_WNDPROC, 8, true), 0x140001000ull);
        T_EQ(rt_window_get_long(kid, GWLP_HWNDPARENT, 8, true), (LONG_PTR)top);
        T_EQ(rt_window_get_long(owned, GWLP_HWNDPARENT, 8, true), (LONG_PTR)top);
        T_EQ(rt_window_get_long(top, GWLP_HWNDPARENT, 8, true), 0);
    }

    T_PLAN(9, "pointer indexes at size 4 are refused") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_get_long(top, GWLP_WNDPROC, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_get_long(top, GWLP_HINSTANCE, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_get_long(top, GWLP_HWNDPARENT, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    }

    T_PLAN(10, "user data, set returns the previous value") {
        LONG_PTR ud = ((LONG_PTR)1 << 32) | 123;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_long(top, GWLP_USERDATA, ud, 8, true), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_window_get_long(top, GWLP_USERDATA, 8, true), ud);
        T_EQ(rt_window_get_long(top, GWLP_USERDATA, 4, true), 123);
        T_EQ(rt_window_set_long(top, GWLP_USERDATA, 5, 8, true), ud);
    }

    T_PLAN(11, "extra bytes of the window") {
        T_EQ(rt_window_set_long(top, 0, (LONG_PTR)0x1122334455667788ull, 8, true), 0);
        T_EQ(rt_window_get_long(top, 0, 4, true), 0x55667788);
        T_EQ(rt_window_get_long(top, 4, 4, true), 0x11223344);
        T_EQ(rt_window_get_long(top, 8, 8, true), 0);
        T_EQ(rt_window_get_long(top, 12, 4, true), 0);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_get_long(top, 13, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_get_long(top, 9, 8, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_long(top, 13, 1, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
    }

    T_PLAN(12, "negative index; no window") {
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_get_long(top, -99, 8, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_long(top, -99, 1, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        for (unsigned i = 0; i < 3; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_get_long(none[i], GWL_STYLE, 8, true), 0);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_set_long(none[i], GWLP_USERDATA, 1, 8, true), 0);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        }
    }

    T_PLAN(13, "pointer indexes at size 4 change nothing") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_long(top, GWLP_WNDPROC, 0x12345678, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        T_EQ(rt_window_get_long(top, GWLP_WNDPROC, 8, true), 0x140001000ull);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_long(top, GWLP_HINSTANCE, 1, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_set_long(kid, GWLP_HWNDPARENT, 0, 4, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        T_EQ(rt_window_get_long(top, GWLP_HINSTANCE, 8, true), 0x140000000ull);
    }

    T_PLAN(14, "procedure, identifier and style are stored") {
        T_EQ(rt_window_set_long(top, GWLP_WNDPROC, (LONG_PTR)0x140009000ull, 8, true), (LONG_PTR)0x140001000ull);
        T_EQ(rt_window_get_long(top, GWLP_WNDPROC, 8, true), 0x140009000ull);
        T_EQ(rt_window_set_long(top, GWLP_WNDPROC, 0, 8, true), (LONG_PTR)0x140009000ull);
        T_EQ(rt_window_get_long(top, GWLP_WNDPROC, 8, true), 0x140009000ull);
        T_EQ(rt_window_set_long(kid, GWL_ID, 77, 4, true), 42);
        T_EQ(rt_window_get_long(kid, GWL_ID, 4, true), 77);
        T_EQ(rt_window_set_long(kid, GWL_STYLE, 0x42000000, 4, true), 0x40000000);
        T_EQ(rt_window_get_long(kid, GWL_STYLE, 4, true), 0x42000000);
    }

    T_PLAN(15, "these stop") {
        T_TRAPS(rt_window_get_long(top, GWLP_WNDPROC, 8, false), "rt_window_get_long: not implemented");
        T_TRAPS(rt_window_set_long(top, GWLP_WNDPROC, 1, 8, false), "rt_window_set_long: not implemented");
        T_TRAPS(rt_window_set_long(kid, GWL_STYLE, 0x50000000, 4, true), "rt_window_set_long: not implemented");
        T_TRAPS(rt_window_set_long(kid, GWL_STYLE, 0x40C00000, 4, true), "rt_window_set_long: not implemented");
        T_TRAPS(rt_window_set_long(kid, GWLP_HWNDPARENT, 0, 8, true), "rt_window_set_long: not implemented");
        T_TRAPS(rt_window_get_long(RT_HWND_DESKTOP, GWL_STYLE, 8, true), "rt_window_get_long: not implemented");
    }
}

TEST(core, win_query_class)
{
    RtWndClass *c = cls(u"QueryClass", 16);
    HWND top = win(c, NULL, WS_POPUP, 0, u"Main", 100, 200, 300, 400);
    HWND kid = win(c, top, WS_CHILD, 42, NULL, 10, 20, 30, 40);
    HWND kid2 = win(c, top, WS_CHILD, 43, NULL, 0, 0, 5, 5);
    HWND owned = win(c, top, WS_POPUP, 0, NULL, 0, 0, 50, 50);
    HWND mo = win(cls(u"MsgOnly", 0), HWND_MESSAGE, 0, 0, u"Main", 0, 0, 0, 0);
    DWORD tid = LD32(T_CTX()->gs_base + 0x48);
    (void)kid2; (void)owned; (void)mo;

    T_PLAN(16, "the class's values, and its zero extra bytes") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_wndclass_get_long(top, GCLP_WNDPROC, true), 0x140001000ull);
        T_EQ(rt_wndclass_get_long(top, GCL_STYLE, true), 0x2b);
        T_EQ(rt_wndclass_get_long(top, GCL_CBWNDEXTRA, true), 16);
        T_EQ(rt_wndclass_get_long(top, GCL_CBCLSEXTRA, true), 16);
        T_EQ(rt_wndclass_get_long(top, GCW_ATOM, true), c->atom);
        T_EQ(rt_wndclass_get_long(top, GCLP_HICON, true), 0x27F00);
        T_EQ(rt_wndclass_get_long(top, GCLP_HICONSM, true), 0x27F01);
        T_EQ(rt_wndclass_get_long(top, GCLP_HCURSOR, true), 0x17F00);
        T_EQ(rt_wndclass_get_long(top, GCLP_HBRBACKGROUND, true), 6);
        T_EQ(rt_wndclass_get_long(top, GCLP_HMODULE, true), 0x140000000ull);
        T_EQ(rt_wndclass_get_long(top, 0, true), 0);
        T_EQ(rt_wndclass_get_long(top, 8, true), 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(17, "invalid indexes, no window, the two stops") {
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_wndclass_get_long(top, 9, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_wndclass_get_long(top, -99, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_INDEX);
        for (unsigned i = 0; i < 3; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_wndclass_get_long(none[i], 0, true), 0);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        }
        T_TRAPS(rt_wndclass_get_long(top, GCLP_MENUNAME, true), "rt_wndclass_get_long: not implemented");
        T_TRAPS(rt_wndclass_get_long(RT_HWND_DESKTOP, GCL_STYLE, true), "rt_wndclass_get_long: not implemented");
    }

    T_PLAN(18, "iconic and visible") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(!rt_window_is_iconic(top));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(!rt_window_is_iconic(RT_HWND_DESKTOP));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(!rt_window_is_visible(top));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_is_visible(RT_HWND_DESKTOP));
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        for (unsigned i = 0; i < 3; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(!rt_window_is_iconic(none[i]));
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(!rt_window_is_visible(none[i]));
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        }
    }

    T_PLAN(19, "owner thread and process") {
        DWORD pid = 0;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_window_thread(top, &pid), tid);
        T_EQ(pid, LD32(T_CTX()->gs_base + 0x40));
        T_EQ(rt_window_thread(kid, NULL), tid);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        for (unsigned i = 0; i < 3; i++) {
            DWORD p = 7;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_window_thread(none[i], &p), 0);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            T_EQ(p, 7);
        }
        T_TRAPS(rt_window_thread(RT_HWND_DESKTOP, NULL), "rt_window_thread: not implemented");
    }
}

TEST(core, win_query_child)
{
    RtWndClass *c = cls(u"QueryClass", 16);
    HWND top = win(c, NULL, WS_POPUP, 0, u"Main", 100, 200, 300, 400);
    HWND kid = win(c, top, WS_CHILD, 42, NULL, 10, 20, 30, 40);
    HWND kid2 = win(c, top, WS_CHILD, 43, NULL, 0, 0, 5, 5);
    HWND owned = win(c, top, WS_POPUP, 0, NULL, 0, 0, 50, 50);
    HWND mo = win(cls(u"MsgOnly", 0), HWND_MESSAGE, 0, 0, u"Main", 0, 0, 0, 0);
    DWORD tid = LD32(T_CTX()->gs_base + 0x48);
    (void)mo; (void)tid;

    T_PLAN(20, "siblings, owner, order") {
        T_TRUE(rt_window_get(top, GW_CHILD) == kid);
        T_TRUE(rt_window_get(kid, GW_HWNDNEXT) == kid2);
        T_TRUE(rt_window_get(kid2, GW_HWNDPREV) == kid);
        T_TRUE(rt_window_get(kid2, GW_HWNDFIRST) == kid);
        T_TRUE(rt_window_get(kid, GW_HWNDLAST) == kid2);
        T_TRUE(rt_window_get(owned, GW_OWNER) == top);
        T_TRUE(rt_window_get(owned, GW_HWNDNEXT) == top);
        T_TRUE(rt_window_get(top, GW_HWNDPREV) == owned);
    }

    T_PLAN(21, "no neighbour, no owner, no child; cmd 6 and 99") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_get(kid2, GW_HWNDNEXT) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_get(kid, GW_HWNDPREV) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_get(top, GW_OWNER) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_get(kid, GW_CHILD) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_get(top, GW_HWNDNEXT) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_get(top, GW_ENABLEDPOPUP) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_get(owned, GW_ENABLEDPOPUP) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_get(top, 99) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        for (unsigned i = 0; i < 3; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(rt_window_get(none[i], GW_CHILD) == NULL);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(rt_window_get(none[i], GW_ENABLEDPOPUP) == NULL);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        }
    }

    T_PLAN(22, "first top-level window; the top child") {
        T_TRUE(rt_window_top(NULL) == owned);
        T_TRUE(rt_window_top(RT_HWND_DESKTOP) == owned);
        T_TRUE(rt_window_top(top) == kid);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_top(kid) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(23, "find by class and title") {
        T_TRUE(rt_window_find(RT_TEXT_W(u"queryclass"), RT_TEXT_W(NULL)) == owned);
        T_TRUE(rt_window_find(RT_TEXT_A("QUERYCLASS"), RT_TEXT_A("Main")) == top);
        T_TRUE(rt_window_find(RT_TEXT_W((const WCHAR *)(uintptr_t)c->atom), RT_TEXT_W(u"Main")) == top);
        T_TRUE(rt_window_find(RT_TEXT_W(NULL), RT_TEXT_W(u"Main")) == top);
        T_TRUE(rt_window_find(RT_TEXT_W(NULL), RT_TEXT_W(u"")) == owned);
        T_TRUE(rt_window_find(RT_TEXT_A("QueryClass"), RT_TEXT_A("")) == owned);
    }

    T_PLAN(24, "no match: NULL, last error unchanged") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_find(RT_TEXT_W(u"QueryClass"), RT_TEXT_W(u"main")) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_find(RT_TEXT_W(u"NoSuchClass"), RT_TEXT_W(NULL)) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_find(RT_TEXT_W(u"MsgOnly"), RT_TEXT_W(NULL)) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_find(RT_TEXT_W(u""), RT_TEXT_W(NULL)) == NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(25, "top-level windows listed, capacity honoured") {
        HWND out[8];
        for (unsigned i = 0; i < 8; i++) out[i] = (HWND)(uintptr_t)1;
        T_EQ(rt_window_list_top(out, 8), 2);
        T_TRUE(out[0] == owned);
        T_TRUE(out[1] == top);
        T_TRUE(out[2] == (HWND)(uintptr_t)1);
        for (unsigned i = 0; i < 8; i++) out[i] = (HWND)(uintptr_t)1;
        T_EQ(rt_window_list_top(out, 1), 2);
        T_TRUE(out[0] == owned);
        T_TRUE(out[1] == (HWND)(uintptr_t)1);
        T_EQ(rt_window_list_top(NULL, 0), 2);
    }

    T_PLAN(26, "child by identifier") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_child_by_id(top, 42) == kid);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(rt_window_child_by_id(top, 43) == kid2);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_window_child_by_id(top, 44) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_CONTROL_ID_NOT_FOUND);
        HWND none[3] = { NULL, (HWND)(uintptr_t)0x40000FF0ull, made_and_dropped(c) };
        for (unsigned i = 0; i < 3; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(rt_window_child_by_id(none[i], 42) == NULL);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        }
    }
}
