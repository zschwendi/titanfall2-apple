/* Window records and the "user" handle table (core/win_priv.h, built in core/win.c). */
#include "rt/test.h"
#include "rt/sync.h"
#include "../../core/win_priv.h"

static RtWndClass cls_a = { .used = true, .atom = 0xC001, .name = u"TestClass", .proc = 0x1234 };

static HWND mk(HWND parent, DWORD style, const WCHAR *title) {
    RtWinNew a = { .cls = &cls_a, .style = style, .rect = { 10, 20, 110, 220 }, .parent = parent,
                   .title = title, .thread_id = 4, .creator = 0x777 };
    return rt_win_window_new(&a);
}

TEST(core, win_table)
{
    T_CASE("a window handle comes from the user table and names its record");
    rt_thread_set_last_error(0xDEAD);
    HWND h = mk(NULL, WS_POPUP, u"Title");
    T_REQUIRE(h != NULL);
    T_TRUE((uintptr_t)h >= 0x40000000u && (uintptr_t)h <= 0x7FFFFFFFu && ((uintptr_t)h & 3) == 0);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    RtWindow *w = rt_win_window_ref(h);
    T_REQUIRE(w != NULL);
    T_TRUE(w->self == h && w->cls == &cls_a && w->proc == 0x1234 && w->style == WS_POPUP);
    T_TRUE(w->rect.left == 10 && w->rect.bottom == 220 && w->thread_id == 4 && w->creator == 0x777);
    T_TRUE(w->text && w->text[0] == 'T' && w->text[5] == 0);
    T_TRUE(!w->message_only && w->parent == NULL && w->owner == NULL && w->up == NULL);
    T_TRUE(rt_win.top == w && rt_win.windows == 1 && cls_a.windows == 1);
    rt_object_unref(&w->obj);

    T_CASE("the two handle spaces do not mix");
    T_TRUE(rt_handle_ref(h, RT_K_ANY) == NULL);                 /* not a kernel handle */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(h), FALSE);
    HANDLE ev = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    rt_thread_set_last_error(0xDEAD);
    T_TRUE(rt_win_window_ref((HWND)ev) == NULL);                /* a kernel handle is not a window */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    static const uintptr_t bad[] = { 0, 4, 0x10010 /* RT_HWND_DESKTOP */, 0xFFFF, (uintptr_t)-3, 0x40000FF0u /* a free slot */, 0x80000000u };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_win_window_ref((HWND)bad[i]) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    }
    rt_handle_close(ev);

    /* Wine user32/tests/win.c: test_children_zorder (children come back in creation order),
     * test_popup_zorder (a new top-level window is on top). */
    T_CASE("top-level windows are linked newest first, children oldest first, message-only windows apart");
    HWND c1 = mk(h, WS_CHILD, NULL), c2 = mk(h, WS_CHILD, u""), owned = mk(h, WS_POPUP, NULL);
    HWND mo = mk(HWND_MESSAGE, 0, NULL);
    T_REQUIRE(c1 && c2 && owned && mo);
    RtWindow *wc1 = rt_win_window_ref(c1), *wc2 = rt_win_window_ref(c2), *wo = rt_win_window_ref(owned),
             *wm = rt_win_window_ref(mo);
    w = rt_win_window_ref(h);
    T_TRUE(w->first_child == wc1 && wc1->next == wc2 && wc2->prev == wc1 && wc2->next == NULL && wc1->prev == NULL);
    T_TRUE(wc1->parent == h && wc1->up == w && wc1->owner == NULL && wc2->text == NULL);
    T_TRUE(wo->owner == h && wo->parent == NULL && wo->up == NULL);
    T_TRUE(rt_win.top == wo && wo->next == w && w->prev == wo);
    T_TRUE(wm->message_only && rt_win.message_only == wm && wm->next == NULL);
    T_TRUE(rt_win.windows == 5 && cls_a.windows == 5);
    rt_thread_set_last_error(0xDEAD);
    T_TRUE(mk((HWND)(uintptr_t)0x40000404u, WS_CHILD, NULL) == NULL);     /* a parent that is no window */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(rt_win.windows == 5);

    T_CASE("a dropped window's handle is invalid and the lists close up");
    atomic_store(&rt_win.focus, c1);
    T_EQ(rt_win_window_drop(c1), TRUE);
    T_TRUE(rt_window_focus() == NULL);                          /* a dropped window holds nothing */
    T_TRUE(w->first_child == wc2 && wc2->next == NULL);
    rt_thread_set_last_error(0xDEAD);
    T_TRUE(rt_win_window_ref(c1) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(rt_win_window_drop(c1), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRAPS(rt_win_window_drop(h), "still has children");
    T_EQ(rt_win_window_drop(c2), TRUE);
    T_EQ(rt_win_window_drop(owned), TRUE);
    T_TRUE(rt_win.top == w && w->prev == NULL);
    T_EQ(rt_win_window_drop(mo), TRUE);
    T_TRUE(rt_win.message_only == NULL);
    T_EQ(rt_win_window_drop(h), TRUE);
    T_TRUE(rt_win.top == NULL && rt_win.windows == 0 && cls_a.windows == 0);
    T_EQ(rt_htab_live(rt_win_table()), 0);
    rt_object_unref(&wc1->obj); rt_object_unref(&wc2->obj); rt_object_unref(&wo->obj);
    rt_object_unref(&wm->obj); rt_object_unref(&w->obj);

    T_CASE("a handle is not handed out again at once");
    HWND again = mk(NULL, WS_POPUP, NULL);
    T_TRUE(again != NULL && again != h && again != c1);
    T_EQ(rt_win_window_drop(again), TRUE);
}
