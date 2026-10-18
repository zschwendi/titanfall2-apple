/* The clipboard of this process (rt/window.h, core/win_clipboard.c): the plan's cases 1 to 12,
 * one T_PLAN block each, in three TESTs that run in order.  A fresh process has a closed, empty
 * clipboard; each case says which state the cases before it left.  Expected values: the comment
 * of the clipboard in rt/window.h, where its Wine and review rows are cited. */
#include <pthread.h>
#include <stdint.h>

#include "rt/test.h"
#include "../../core/win_priv.h"

/* A handle as a 64-bit value, for T_EQ. */
static uint64_t hv(HANDLE h) { return (uint64_t)(uintptr_t)h; }

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

TEST(core, win_clipboard)
{
    T_PLAN(1, "before any open: close, empty, set and get fail with ERROR_CLIPBOARD_NOT_OPEN") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_close(), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_empty(), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_set(CF_TEXT, (HANDLE)0x1000)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_get(CF_TEXT)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
    }

    T_PLAN(2, "NULL owner: open is TRUE, a second open is TRUE, one close closes, a second close is FALSE") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_open(NULL), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_clipboard_open(NULL), TRUE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_close(), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_close(), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
    }
    T_PLAN(3, "owner: an invalid window is refused first; another owner is ACCESS_DENIED while the caller has it open") {
        RtWndClass *c = cls(u"ClipWin", 0);
        HWND w = win(c, NULL, WS_POPUP, 0, NULL, 0, 0, 10, 10);
        T_REQUIRE(c != NULL && w != NULL);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_open((HWND)0x40000FF0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        T_EQ(rt_clipboard_close(), FALSE);
        T_EQ(rt_clipboard_open(w), TRUE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_open(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(rt_clipboard_open(w), TRUE);
        T_EQ(rt_clipboard_close(), TRUE);
        T_EQ(rt_clipboard_close(), FALSE);
        T_EQ(rt_clipboard_open(NULL), TRUE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_open(w), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(rt_clipboard_open(NULL), TRUE);
        T_EQ(rt_clipboard_close(), TRUE);
    }
}

TEST(core, win_clipboard_get)
{
    T_PLAN(4, "fresh process: after open, get of CF_TEXT and of format 2 is NULL with ERROR_NOT_FOUND") {
        BOOL opened = rt_clipboard_open(NULL);
        T_REQUIRE(opened);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_get(CF_TEXT)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_get(2)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);
    }

    T_PLAN(5, "set returns its handle with last error unchanged; setting the format again replaces it") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_set(CF_UNICODETEXT, (HANDLE)0x5000)), 0x5000);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(hv(rt_clipboard_get(CF_UNICODETEXT)), 0x5000);
        T_EQ(hv(rt_clipboard_set(CF_UNICODETEXT, (HANDLE)0x6000)), 0x6000);
        T_EQ(hv(rt_clipboard_get(CF_UNICODETEXT)), 0x6000);
    }

    T_PLAN(6, "after close and open(NULL) the CF_UNICODETEXT handle is still there") {
        T_TRUE(rt_clipboard_close());
        BOOL opened = rt_clipboard_open(NULL);
        T_REQUIRE(opened);
        T_EQ(hv(rt_clipboard_get(CF_UNICODETEXT)), 0x6000);
    }

    T_PLAN(7, "get of CF_TEXT and of CF_OEMTEXT stops while only CF_UNICODETEXT is set; get of 2 is NULL") {
        T_TRAPS(rt_clipboard_get(CF_TEXT), "rt_clipboard_get: not implemented");
        T_TRAPS(rt_clipboard_get(CF_OEMTEXT), "rt_clipboard_get: not implemented");
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_get(2)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);
    }

    T_PLAN(8, "empty is TRUE with last error unchanged; afterwards the text formats are NULL") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_clipboard_empty(), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_get(CF_UNICODETEXT)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_get(CF_TEXT)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);
    }

    T_PLAN(9, "set: NULL data stops; format 0 is refused while open; a number nobody registered is accepted") {
        T_TRAPS(rt_clipboard_set(CF_TEXT, NULL), "rt_clipboard_set: not implemented");
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_set(0, (HANDLE)0x1000)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_set(0, NULL)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_CLIPBOARD_NOT_OPEN);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(hv(rt_clipboard_get(0)), 0);
        T_EQ(T_LAST_ERROR(), ERROR_NOT_FOUND);
        T_EQ(hv(rt_clipboard_set(0x1234, (HANDLE)0x1000)), 0x1000);
    }
}

/* The second threads of cases 11 and 12: what they saw, checked by the main thread after the
 * join.  They make no T_ check. */
static BOOL   t11_open, t11_close;
static DWORD  t11_open_le, t11_close_le, t11_get_le;
static HANDLE t11_get;
static BOOL   t12_open, t12_close;
static HANDLE t12_get;

static void *t11_run(void *arg) {
    (void)arg;
    rt_thread_set_last_error(0xDEAD);
    t11_open = rt_clipboard_open(NULL);
    t11_open_le = rt_thread_get_last_error();
    rt_thread_set_last_error(0xDEAD);
    t11_close = rt_clipboard_close();
    t11_close_le = rt_thread_get_last_error();
    rt_thread_set_last_error(0xDEAD);
    t11_get = rt_clipboard_get(0xC000);
    t11_get_le = rt_thread_get_last_error();
    return NULL;
}

static void *t12_run(void *arg) {
    (void)arg;
    t12_open = rt_clipboard_open(NULL);
    t12_get = rt_clipboard_get(0xC000);
    t12_close = rt_clipboard_close();
    return NULL;
}

TEST(core, win_clipboard_slots)
{
    T_PLAN(10, "fresh process: 16 formats are set and each returns its handle; a set format is replaced; a 17th stops") {
        BOOL opened = rt_clipboard_open(NULL);
        T_REQUIRE(opened);
        for (unsigned i = 0; i < 16; i++) {
            T_EQ(hv(rt_clipboard_set((UINT)(0xC000 + i), (HANDLE)(uintptr_t)(0x1000 + i))), 0x1000 + i);
        }
        T_EQ(hv(rt_clipboard_set(0xC000 + 3, (HANDLE)0x9000)), 0x9000);
        T_TRAPS(rt_clipboard_set(0xC000 + 16, (HANDLE)0x1010), "rt_clipboard_set: not implemented");
        T_EQ(hv(rt_clipboard_get(0xC000 + 15)), 0x100F);
    }

    T_PLAN(11, "while the main thread has it open, another thread: open ACCESS_DENIED, close and get NOT_OPEN") {
        pthread_t th;
        int rc = pthread_create(&th, NULL, t11_run, NULL);
        T_REQUIRE(rc == 0);
        rc = pthread_join(th, NULL);
        T_REQUIRE(rc == 0);
        T_EQ(t11_open, FALSE);
        T_EQ(t11_open_le, ERROR_ACCESS_DENIED);
        T_EQ(t11_close, FALSE);
        T_EQ(t11_close_le, ERROR_CLIPBOARD_NOT_OPEN);
        T_EQ(hv(t11_get), 0);
        T_EQ(t11_get_le, ERROR_CLIPBOARD_NOT_OPEN);
    }

    T_PLAN(12, "after the main thread closes, another thread opens, gets 0xC000 and closes") {
        T_TRUE(rt_clipboard_close());
        pthread_t th;
        int rc = pthread_create(&th, NULL, t12_run, NULL);
        T_REQUIRE(rc == 0);
        rc = pthread_join(th, NULL);
        T_REQUIRE(rc == 0);
        T_EQ(t12_open, TRUE);
        T_EQ(hv(t12_get), 0x1000);
        T_EQ(t12_close, TRUE);
    }
}
