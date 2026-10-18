/* Hook records (rt/window.h, core/win_hook.c): the test plan of docs/handoff/rt-window-core-1.md,
 * cases 1 to 10, typed as written.  One TEST: the cases run in its process, one block each. */
#include "rt/test.h"
#include "../../core/win_priv.h"

/* Copied as they are from docs/handoff/rt-window-core-1.md: the plans call them. */
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

TEST(core, win_hook)
{
    HOOKPROC proc = (HOOKPROC)0x140003000ull;
    HINSTANCE mod = (HINSTANCE)0x140000000ull;
    DWORD tid = LD32(T_CTX()->gs_base + 0x48);

    T_PLAN(1, "a keyboard LL hook on no thread: a handle, LE unchanged, one object live") {
        rt_thread_set_last_error(0xDEAD);
        HHOOK h = rt_hook_set(WH_KEYBOARD_LL, proc, NULL, 0);
        T_TRUE(h != NULL);
        T_TRUE((uint64_t)(uintptr_t)h >= 0x40000000ull);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_htab_live(rt_win_table()), 1);
    }

    T_PLAN(2, "three hooks of three types: three handles, all different") {
        HHOOK h1 = rt_hook_set(WH_MOUSE, proc, NULL, tid);
        HHOOK h2 = rt_hook_set(WH_KEYBOARD, proc, mod, 0);
        HHOOK h3 = rt_hook_set(WH_MOUSE_LL, proc, mod, 0);
        T_TRUE(h1 != NULL && h2 != NULL && h3 != NULL);
        T_TRUE(h1 != h2 && h1 != h3 && h2 != h3);
    }

    T_PLAN(3, "remove a hook: TRUE with LE unchanged, then FALSE with ERROR_INVALID_HOOK_HANDLE") {
        HHOOK h = rt_hook_set(WH_KEYBOARD, proc, NULL, tid);
        T_TRUE(h != NULL);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_hook_remove(h), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_hook_remove(h), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_HANDLE);
    }

    T_PLAN(4, "remove what is no hook: FALSE with ERROR_INVALID_HOOK_HANDLE; the window stays") {
        RtWndClass *c = cls(u"HookWin", 0);
        T_REQUIRE(c != NULL);
        HWND w = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 10, 10);
        T_TRUE(w != NULL);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_hook_remove(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_hook_remove((HHOOK)(uintptr_t)0xdeadbeef), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_hook_remove((HHOOK)w), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_HANDLE);
        T_EQ(rt_win_window_drop(w), TRUE);
    }

    T_PLAN(5, "a filter type outside -1 to 14: NULL with ERROR_INVALID_HOOK_FILTER") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(99, proc, mod, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_FILTER);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(-2, proc, mod, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_FILTER);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(15, 0, NULL, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_FILTER);
    }

    T_PLAN(6, "a procedure of 0: NULL with ERROR_INVALID_FILTER_PROC, whatever the type") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(WH_KEYBOARD, 0, NULL, tid) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_FILTER_PROC);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(5, 0, NULL, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_FILTER_PROC);
    }

    T_PLAN(7, "a _LL hook on one thread: NULL with ERROR_GLOBAL_ONLY_HOOK") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(WH_MOUSE_LL, proc, mod, tid) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_GLOBAL_ONLY_HOOK);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(WH_KEYBOARD_LL, proc, NULL, tid) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_GLOBAL_ONLY_HOOK);
    }

    T_PLAN(8, "a global hook with no module: NULL with ERROR_HOOK_NEEDS_HMOD") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(WH_KEYBOARD, proc, NULL, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_HOOK_NEEDS_HMOD);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(WH_MOUSE, proc, NULL, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_HOOK_NEEDS_HMOD);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_hook_set(5, proc, NULL, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_HOOK_NEEDS_HMOD);
    }

    T_PLAN(9, "the valid types that are not built stop") {
        T_TRAPS(rt_hook_set(5, proc, mod, 0), "rt_hook_set: not implemented");
        T_TRAPS(rt_hook_set(3, proc, NULL, tid), "rt_hook_set: not implemented");
        T_TRAPS(rt_hook_set(WH_KEYBOARD, proc, NULL, tid + 4), "rt_hook_set: not implemented");
    }

    T_PLAN(10, "failed sets and a set that is removed leave the live count as it was") {
        uint64_t before = rt_htab_live(rt_win_table());
        T_TRUE(rt_hook_set(99, proc, mod, 0) == NULL);
        T_TRUE(rt_hook_set(WH_KEYBOARD, proc, NULL, 0) == NULL);
        HHOOK m = rt_hook_set(WH_MOUSE, proc, NULL, tid);
        T_TRUE(m != NULL);
        T_EQ(rt_hook_remove(m), TRUE);
        T_EQ(rt_htab_live(rt_win_table()), before);
    }
}
