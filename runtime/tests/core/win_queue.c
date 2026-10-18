/* The message queue of a thread (rt/window.h, "the message queue"; core/win_queue.c).
 *
 * Where each expected value comes from: the comment of the function in rt/window.h, which
 * names its source for every statement (a Microsoft page, a test of Wine, a row of
 * docs/reviews/contracts-check.md or contracts-check-2.md, or "from memory").  Nothing here
 * was read out of game code.  The cases a comment marks as stops are proved as stops.
 *
 * Threads: the workers of wait_util.c (host threads that each own a hand-made guest thread).
 * A window "of a worker" is a record made by the main thread with the worker's thread id.
 */
#include "rt/test.h"
#include "../../core/win_priv.h"
#include "wait_util.c"

#define CTX     T_CTX()
#define MY_ID   LD32(T_CTX()->gs_base + 0x48)
#define NOWIN   ((HWND)(uintptr_t)0x40000FF0)       /* a free slot of the user table */
#define STOPS(call, fn)  T_TRAPS((void)(call), fn ": not implemented")

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

/* A window record of thread `tid`. */
static HWND win_of(RtWndClass *c, DWORD tid, HWND parent, DWORD style) {
    RtWinNew a = { .cls = c, .style = style, .rect = { 0, 0, 100, 100 }, .parent = parent,
                   .instance = (HINSTANCE)0x140000000ull, .title = NULL, .unicode = true,
                   .thread_id = tid, .creator = 0x140002000ull };
    return rt_win_window_new(&a);
}

static BOOL peek(MSG *m, HWND h, UINT min, UINT max, UINT flags) { return rt_msg_peek(CTX, m, h, min, max, flags, true); }
static BOOL post(HWND h, UINT msg, WPARAM w, LPARAM l) { return rt_msg_post(CTX, h, msg, w, l); }

static bool all_a5(const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) if (((const uint8_t *)p)[i] != 0xA5) return false;
    return true;
}

/* Empty the calling thread's queue; how many messages it held. */
static unsigned drain(void) {
    MSG m;
    unsigned n = 0;
    while (peek(&m, NULL, 0, 0, PM_REMOVE)) n++;
    return n;
}

TEST(core, win_queue_peek)
{
    RtWndClass *c = cls(u"Q", 0);
    T_REQUIRE(c != NULL);
    HWND top = win_of(c, MY_ID, NULL, 0), child = win_of(c, MY_ID, top, WS_CHILD), other = win_of(c, MY_ID, NULL, 0);
    T_REQUIRE(top && child && other);
    MSG m;

    T_CASE("an empty queue: FALSE at once, every time, the MSG and last error untouched");
    rt_thread_set_last_error(0xDEAD);
    for (unsigned i = 0; i < 1000; i++) {
        memset(&m, 0xA5, sizeof m);
        T_EQ(peek(&m, NULL, 0, 0, i % 3 == 0 ? PM_REMOVE : i % 3 == 1 ? PM_NOREMOVE : PM_REMOVE | PM_NOYIELD), FALSE);
        T_TRUE(all_a5(&m, sizeof m));
    }
    T_EQ(peek(&m, top, 0, 0, PM_REMOVE), FALSE);
    T_EQ(peek(&m, (HWND)(intptr_t)-1, WM_USER, WM_USER + 9, PM_REMOVE), FALSE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("a thread message: seen by every PM_NOREMOVE peek, taken by one PM_REMOVE peek");
    uint64_t before = rt_time_tick_ms();
    T_EQ(post(NULL, WM_USER + 7, 1, 2), TRUE);
    uint64_t after = rt_time_tick_ms();
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    for (int i = 0; i < 2; i++) {
        memset(&m, 0xA5, sizeof m);
        T_EQ(peek(&m, NULL, 0, 0, PM_NOREMOVE), TRUE);
        T_TRUE(m.hwnd == NULL);
        T_EQ(m.message, WM_USER + 7);
        T_EQ(m.wParam, 1);
        T_EQ(m.lParam, 2);
    }
    memset(&m, 0xA5, sizeof m);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.hwnd == NULL && m.message == WM_USER + 7 && m.wParam == 1 && m.lParam == 2);
    T_TRUE(m.time >= (DWORD)before && m.time <= (DWORD)after);          /* rt_time_tick_ms at the post */
    T_EQ(m.pt.x, 960);                                                  /* the cursor's starting point */
    T_EQ(m.pt.y, 540);
    T_TRUE(all_a5((uint8_t *)&m + 12, 4) && all_a5((uint8_t *)&m + 44, 4));   /* the padding of a MSG is not written */
    T_EQ(sizeof m, 48);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("messages come out in the order posted");
    for (unsigned i = 0; i < 40; i++) T_EQ(post(i % 2 ? top : NULL, WM_USER + i, i, ~(LPARAM)i), TRUE);
    for (unsigned i = 0; i < 40; i++) {
        T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), TRUE);
        T_TRUE(m.hwnd == (i % 2 ? top : NULL) && m.message == WM_USER + i && m.wParam == i && m.lParam == ~(LPARAM)i);
    }
    T_EQ(drain(), 0);

    T_CASE("the range filter");
    T_EQ(post(NULL, WM_USER + 1, 0, 0), TRUE);
    T_EQ(post(NULL, WM_USER + 5, 0, 0), TRUE);
    memset(&m, 0xA5, sizeof m);
    T_EQ(peek(&m, NULL, WM_USER + 2, WM_USER + 4, PM_REMOVE), FALSE);
    T_EQ(peek(&m, NULL, WM_APP, 0xFFFF, PM_REMOVE), FALSE);
    T_EQ(peek(&m, NULL, 0, WM_USER, PM_REMOVE), FALSE);
    T_TRUE(all_a5(&m, sizeof m));
    T_EQ(peek(&m, NULL, WM_USER + 2, WM_USER + 5, PM_REMOVE), TRUE);    /* the second, past the first */
    T_EQ(m.message, WM_USER + 5);
    T_EQ(peek(&m, NULL, WM_USER + 1, WM_USER + 1, PM_REMOVE), TRUE);
    T_EQ(m.message, WM_USER + 1);
    T_EQ(drain(), 0);

    T_CASE("the window filter: -1 is thread messages only, a window is itself and its children");
    T_EQ(post(top, WM_USER + 1, 0, 0), TRUE);
    T_EQ(post(NULL, WM_USER + 2, 0, 0), TRUE);
    T_EQ(post(child, WM_USER + 3, 0, 0), TRUE);
    T_EQ(post(other, WM_USER + 4, 0, 0), TRUE);
    T_EQ(peek(&m, (HWND)(intptr_t)-1, 0, 0, PM_NOREMOVE), TRUE);        /* review D5's test */
    T_TRUE(m.hwnd == NULL && m.message == WM_USER + 2);
    T_EQ(peek(&m, child, 0, 0, PM_NOREMOVE), TRUE);
    T_TRUE(m.hwnd == child && m.message == WM_USER + 3);
    T_EQ(peek(&m, other, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.hwnd == other && m.message == WM_USER + 4);
    T_EQ(peek(&m, other, 0, 0, PM_REMOVE), FALSE);
    T_EQ(peek(&m, top, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.hwnd == top && m.message == WM_USER + 1);
    T_EQ(peek(&m, top, 0, 0, PM_REMOVE), TRUE);                         /* the child's, through its parent */
    T_TRUE(m.hwnd == child && m.message == WM_USER + 3);
    T_EQ(peek(&m, top, 0, 0, PM_REMOVE), FALSE);
    T_EQ(peek(&m, (HWND)(intptr_t)-1, 0, 0, PM_REMOVE), TRUE);
    T_EQ(drain(), 0);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("a filter that is not a window: FALSE with ERROR_INVALID_WINDOW_HANDLE, the MSG untouched");
    HWND gone = win_of(c, MY_ID, NULL, 0);
    T_REQUIRE(gone && rt_win_window_drop(gone));
    T_EQ(post(NULL, WM_USER, 0, 0), TRUE);
    HWND bad[] = { NOWIN, gone, (HWND)(uintptr_t)0xdeadbeef, HWND_MESSAGE };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(&m, 0xA5, sizeof m);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(peek(&m, bad[i], 0, 0, PM_REMOVE), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
        T_TRUE(all_a5(&m, sizeof m));
    }
    T_EQ(drain(), 1);

    T_CASE("the messages of a window go with the window");
    HWND doomed = win_of(c, MY_ID, NULL, 0);
    T_REQUIRE(doomed != NULL);
    T_EQ(post(doomed, WM_USER + 1, 0, 0), TRUE);
    T_EQ(post(NULL, WM_USER + 2, 0, 0), TRUE);
    T_EQ(post(doomed, WM_USER + 3, 0, 0), TRUE);
    T_EQ(rt_win_window_drop(doomed), TRUE);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.hwnd == NULL && m.message == WM_USER + 2);
    T_EQ(drain(), 0);

    T_CASE("a posted WM_QUIT is a message like another when no range is given");
    T_EQ(post(NULL, WM_QUIT, 0xbeef, 0), TRUE);
    T_EQ(post(NULL, WM_USER, 0, 0), TRUE);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.message == WM_QUIT && m.wParam == 0xbeef);
    T_EQ(peek(&m, NULL, 0, WM_QUIT, PM_NOREMOVE), FALSE);
    T_EQ(drain(), 1);

    HWND foreign = win_of(c, MY_ID + 0x4000, NULL, 0);
    T_REQUIRE(foreign != NULL);
    STOPS(peek(NULL, NULL, 0, 0, PM_REMOVE), "rt_msg_peek");
    STOPS(peek(&m, NULL, 0, 0, 4), "rt_msg_peek");
    STOPS(peek(&m, NULL, 0, 0, PM_REMOVE | (QS_POSTMESSAGE << 16)), "rt_msg_peek");
    STOPS(peek(&m, (HWND)(uintptr_t)1, 0, 0, PM_REMOVE), "rt_msg_peek");
    STOPS(peek(&m, (HWND)(uintptr_t)0xFFFF, 0, 0, PM_REMOVE), "rt_msg_peek");
    STOPS(peek(&m, RT_HWND_DESKTOP, 0, 0, PM_REMOVE), "rt_msg_peek");
    STOPS(peek(&m, foreign, 0, 0, PM_REMOVE), "rt_msg_peek");
    STOPS(peek(&m, NULL, 0x10000, 0x10001, PM_REMOVE), "rt_msg_peek");
    STOPS(peek(&m, NULL, WM_USER + 1, WM_USER, PM_REMOVE), "rt_msg_peek");
    T_EQ(post(NULL, WM_QUIT, 0, 0), TRUE);
    STOPS(peek(&m, NULL, WM_USER, WM_USER + 1, PM_REMOVE), "rt_msg_peek");
}

TEST(core, win_queue_post)
{
    RtWndClass *c = cls(u"Q", 0);
    T_REQUIRE(c != NULL);
    HWND mine = win_of(c, MY_ID, NULL, 0), foreign = win_of(c, MY_ID + 0x4000, NULL, 0), gone = win_of(c, MY_ID, NULL, 0);
    T_REQUIRE(mine && foreign && gone && rt_win_window_drop(gone));
    MSG m;

    T_CASE("a post to a window of the thread arrives with its four values; last error left alone");
    rt_thread_set_last_error(0xDEAD);
    T_EQ(post(mine, WM_APP + 3, 0x1122334455667788ull, -5), TRUE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.hwnd == mine && m.message == WM_APP + 3 && m.wParam == 0x1122334455667788ull && m.lParam == -5);

    T_CASE("a post to a window of another thread goes to that thread's queue, not the caller's");
    T_EQ(post(foreign, WM_USER, 1, 2), TRUE);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("a post to what is not a window: FALSE with ERROR_INVALID_WINDOW_HANDLE, nothing queued");
    HWND bad[] = { NOWIN, gone, (HWND)(uintptr_t)0xdeadbeef, HWND_MESSAGE };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(post(bad[i], WM_USER, 0, 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    }
    T_EQ(drain(), 0);

    T_CASE("the messages that are let through, and the stops for the rest");
    UINT ok[] = { WM_NULL, WM_CLOSE, WM_QUIT, 0x111, 0x112, WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_SYSKEYDOWN, 0x107, 0x200, 0x20E,
                  WM_USER, 0x7FFF, WM_APP, 0xBFFF, 0xC000, 0xFFFF };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        T_EQ(post(NULL, ok[i], 0x41, 0), TRUE);
        T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), TRUE);
        T_TRUE(m.message == ok[i] && m.wParam == 0x41);
    }
    T_EQ(post(NULL, WM_CHAR, 0x7F, 0), TRUE);
    T_EQ(drain(), 1);
    STOPS(post(HWND_BROADCAST, WM_USER, 0, 0), "rt_msg_post");
    STOPS(post(RT_HWND_DESKTOP, WM_USER, 0, 0), "rt_msg_post");
    STOPS(post((HWND)(intptr_t)-1, WM_USER, 0, 0), "rt_msg_post");
    STOPS(post(NULL, 0x10000, 0, 0), "rt_msg_post");
    STOPS(post(NULL, WM_SETTEXT, 0, 0), "rt_msg_post");
    STOPS(post(NULL, WM_CREATE, 0, 0), "rt_msg_post");
    STOPS(post(NULL, WM_TIMER, 0, 0), "rt_msg_post");
    STOPS(post(NULL, 0x3FF, 0, 0), "rt_msg_post");
    STOPS(post(NULL, 0x108, 0, 0), "rt_msg_post");
    STOPS(post(NULL, WM_CHAR, 0x80, 0), "rt_msg_post");
    STOPS(post(mine, 0x106, 0xE9, 0), "rt_msg_post");
}

TEST(core, win_queue_limit)
{
    T_CASE("a queue takes 10,000 messages (PostMessage's page) and keeps their order; one more stops");
    for (unsigned i = 0; i < 10000; i++) T_REQUIRE(post(NULL, WM_USER, i, 0));
    STOPS(post(NULL, WM_USER, 10000, 0), "rt_msg_post");
    MSG m;
    for (unsigned i = 0; i < 10000; i++) T_REQUIRE(peek(&m, NULL, 0, 0, PM_REMOVE) && m.wParam == i);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_EQ(post(NULL, WM_USER, 0, 0), TRUE);
}

TEST(core, win_queue_time)
{
    MSG m;
    T_CASE("0 before the thread has taken a message; then the time of the last one taken");
    T_EQ(rt_msg_time(), 0);
    T_EQ(post(NULL, WM_USER, 0, 0), TRUE);
    T_EQ(rt_msg_time(), 0);
    T_EQ(peek(&m, NULL, 0, 0, PM_NOREMOVE), TRUE);
    T_EQ(rt_msg_time(), 0);                         /* looked at, not taken */
    rt_thread_set_last_error(0xDEAD);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_EQ(rt_msg_time(), (LONG)m.time);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    usleep(3000);
    T_EQ(post(NULL, WM_USER + 1, 0, 0), TRUE);
    LONG first = rt_msg_time();
    T_EQ(rt_msg_get(CTX, &m, NULL, 0, 0, true), 1);
    T_EQ(rt_msg_time(), (LONG)m.time);
    T_TRUE(rt_msg_time() - first >= 2);
}

/* ---- rt_msg_get ----------------------------------------------------------------------------------- */

typedef struct { HWND window; int ready; unsigned count; } Feed;      /* window: of the worker, made by the main thread */

static void get_loop(Worker *w) {
    Feed *f = &((Feed *)w->arg)[w->index];
    while (!wu_load(&f->ready)) usleep(100);
    MSG m;
    for (unsigned i = 0; i < f->count; i++) {
        memset(&m, 0xA5, sizeof m);
        INT r = rt_msg_get(W_CTX(w), &m, NULL, 0, 0, true);
        W_CHECK(w, r == 1 && m.hwnd == f->window && m.message == WM_USER + w->index && m.wParam == i);
        w->n[0] += r == 1;
    }
    w->result = (DWORD)rt_msg_get(W_CTX(w), &m, NULL, 0, 0, true);      /* the WM_QUIT that ends it */
    W_CHECK(w, m.message == WM_QUIT && m.wParam == 77);
}

TEST(core, win_queue_get)
{
    RtWndClass *c = cls(u"Q", 0);
    T_REQUIRE(c != NULL);
    HWND mine = win_of(c, MY_ID, NULL, 0);
    T_REQUIRE(mine != NULL);
    MSG m;

    T_CASE("1 for a posted message, 0 for WM_QUIT, -1 for a filter that is not a window");
    rt_thread_set_last_error(0xDEAD);
    T_EQ(post(mine, WM_USER + 9, 3, 4), TRUE);
    T_EQ(post(NULL, WM_QUIT, 0xdead, 0), TRUE);
    T_EQ(post(NULL, WM_USER + 1, 0, 0), TRUE);
    memset(&m, 0xA5, sizeof m);
    T_EQ(rt_msg_get(CTX, &m, NULL, 0, 0, false), 1);
    T_TRUE(m.hwnd == mine && m.message == WM_USER + 9 && m.wParam == 3 && m.lParam == 4);
    T_EQ(rt_msg_get(CTX, &m, NULL, 0, 0, false), 0);
    T_TRUE(m.hwnd == NULL && m.message == WM_QUIT && m.wParam == 0xdead);
    T_EQ(rt_msg_get(CTX, &m, (HWND)(intptr_t)-1, WM_USER, WM_USER + 1, true), 1);
    T_EQ(m.message, WM_USER + 1);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    memset(&m, 0xA5, sizeof m);
    T_EQ(rt_msg_get(CTX, &m, NOWIN, 0, 0, true), -1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(all_a5(&m, sizeof m));
    STOPS(rt_msg_get(CTX, NULL, NULL, 0, 0, true), "rt_msg_get");
    T_TRUE(!T_LOGGED(RT_CH_WINDOW, "waits on an empty queue"));

    T_CASE("on an empty queue it sleeps until a post, and every message arrives once and in order");
    enum { N = 4, K = 300 };
    static Worker workers[N];
    static Feed feed[N];
    wu_start(workers, N, get_loop, feed);
    for (unsigned i = 0; i < N; i++) {
        feed[i].window = win_of(c, workers[i].t.id, NULL, 0);
        feed[i].count = K;
        T_REQUIRE(feed[i].window != NULL);
        wu_store(&feed[i].ready, 1);
    }
    /* Every worker has gone to sleep on its empty queue once: the line is written before the sleep. */
    char line[80];
    for (unsigned i = 0; i < N; i++) {
        snprintf(line, sizeof line, "GetMessage: thread 0x%x waits on an empty queue", (unsigned)workers[i].t.id);
        T_TRUE(EVENTUALLY(T_LOGGED(RT_CH_WINDOW, line)));
    }
    for (unsigned k = 0; k < K; k++)
        for (unsigned i = 0; i < N; i++) T_REQUIRE(post(feed[i].window, WM_USER + i, k, 0));
    for (unsigned i = 0; i < N; i++) T_REQUIRE(post(feed[i].window, WM_QUIT, 77, 0));
    T_REQUIRE(wu_finish(workers, N, "rt_msg_get"));
    T_EQ(wu_tally(workers, N, 0), N * K);
    for (unsigned i = 0; i < N; i++) T_EQ(workers[i].result, 0);
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), FALSE);                       /* nothing of it came to the poster */
}

/* ---- many posters, one owner ---------------------------------------------------------------------- */

typedef struct { HWND window; unsigned count; } Target;

static void post_loop(Worker *w) {
    Target *t = w->arg;
    for (unsigned i = 0; i < t->count; i++) {
        BOOL ok = rt_msg_post(W_CTX(w), t->window, WM_USER + w->index, i, (LPARAM)w->t.id);
        W_CHECK(w, ok == TRUE);
        if (wu_rnd(w) % 8 == 0) sched_yield();
    }
}

TEST(core, win_queue_threads)
{
    enum { N = 6, K = 1000 };
    RtWndClass *c = cls(u"Q", 0);
    T_REQUIRE(c != NULL);
    static Worker workers[N];
    Target t = { win_of(c, MY_ID, NULL, 0), K };
    T_REQUIRE(t.window != NULL);

    T_CASE("six threads post to one window: its thread gets every message once, each poster's in order");
    wu_start(workers, N, post_loop, &t);
    unsigned next[N] = { 0 }, got = 0;
    MSG m;
    while (got < N * K) {
        /* Peek and get in turn, so that both ways of looking meet posts in flight. */
        if (got % 2) T_REQUIRE(rt_msg_get(CTX, &m, got % 4 == 1 ? t.window : NULL, 0, 0, true) == 1);
        else if (!peek(&m, NULL, 0, 0, PM_REMOVE)) { T_REQUIRE(wu_done_count(workers, N) < N || peek(&m, NULL, 0, 0, PM_NOREMOVE)); continue; }
        unsigned who = m.message - WM_USER;
        T_REQUIRE(who < N && m.hwnd == t.window && m.wParam == next[who] && (DWORD)m.lParam == workers[who].t.id);
        next[who]++;
        got++;
    }
    T_REQUIRE(wu_finish(workers, N, "rt_msg_post"));
    T_EQ(peek(&m, NULL, 0, 0, PM_REMOVE), FALSE);
    for (unsigned i = 0; i < N; i++) T_EQ(next[i], K);
}

/* ---- rt_msg_wait ---------------------------------------------------------------------------------- */

static void wait_then_get(Worker *w) {
    Feed *f = w->arg;
    while (!wu_load(&f->ready)) usleep(100);
    wu_store(&f->ready, 2);
    w->result = rt_msg_wait(W_CTX(w), 0, NULL, FALSE, INFINITE, QS_ALLINPUT);
    MSG m;
    W_CHECK(w, rt_msg_peek(W_CTX(w), &m, NULL, 0, 0, PM_REMOVE, true) == TRUE && m.hwnd == f->window && m.wParam == 5);
    w->n[0] = rt_msg_wait(W_CTX(w), 0, NULL, FALSE, 30, QS_POSTMESSAGE);
}

TEST(core, win_queue_wait)
{
    MSG m;
    HANDLE unset = rt_event_create(CTX, TRUE, FALSE, NULL), set = rt_event_create(CTX, TRUE, TRUE, NULL);
    T_REQUIRE(unset && set);

    T_CASE("nobody posts: it is the runtime's wait with the same time limit");
    rt_thread_set_last_error(0xDEAD);
    uint64_t t0 = rt_time_mono_100ns();
    T_EQ(rt_msg_wait(CTX, 0, NULL, FALSE, 20, QS_ALLINPUT), WAIT_TIMEOUT);
    T_TRUE(rt_time_mono_100ns() - t0 >= 200000);
    T_EQ(rt_msg_wait(CTX, 1, &unset, FALSE, 0, QS_ALLEVENTS), WAIT_TIMEOUT);
    T_EQ(rt_msg_wait(CTX, 1, &set, FALSE, INFINITE, QS_POSTMESSAGE), WAIT_OBJECT_0);
    HANDLE two[2] = { unset, set };
    T_EQ(rt_msg_wait(CTX, 2, two, FALSE, INFINITE, QS_ALLINPUT), WAIT_OBJECT_0 + 1);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("a handle that is not open: WAIT_FAILED with ERROR_INVALID_HANDLE");
    HANDLE bad[2] = { set, (HANDLE)(uintptr_t)0x7654 };
    T_EQ(rt_msg_wait(CTX, 2, bad, FALSE, 0, QS_ALLINPUT), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a posted message wakes it as WAIT_OBJECT_0 + n, until the thread has looked at its queue");
    rt_thread_set_last_error(0xDEAD);
    T_EQ(post(NULL, WM_USER, 0, 0), TRUE);
    T_EQ(rt_msg_wait(CTX, 1, &unset, FALSE, INFINITE, QS_ALLINPUT), WAIT_OBJECT_0 + 1);
    STOPS(rt_msg_wait(CTX, 1, &unset, FALSE, 0, QS_ALLINPUT), "rt_msg_wait");       /* a second wait, no look between */
    T_EQ(peek(&m, NULL, 0, 0, PM_NOREMOVE), TRUE);
    T_EQ(rt_msg_wait(CTX, 1, &unset, FALSE, 20, QS_ALLINPUT), WAIT_TIMEOUT);        /* still queued, no longer new */
    T_EQ(rt_msg_wait(CTX, 0, NULL, FALSE, 0, QS_ALLINPUT), WAIT_TIMEOUT);
    T_EQ(post(NULL, WM_USER + 1, 0, 0), TRUE);
    T_EQ(rt_msg_wait(CTX, 0, NULL, TRUE, INFINITE, QS_POSTMESSAGE), WAIT_OBJECT_0);  /* n 0: the queue alone */
    T_EQ(peek(&m, NULL, WM_APP, WM_APP, PM_REMOVE), FALSE);                         /* a look that takes nothing */
    T_EQ(rt_msg_wait(CTX, 0, NULL, FALSE, 0, QS_ALLINPUT), WAIT_TIMEOUT);
    T_EQ(drain(), 2);

    T_CASE("a signalled handle comes before the queue");
    T_EQ(post(NULL, WM_USER, 0, 0), TRUE);
    T_EQ(rt_msg_wait(CTX, 1, &set, FALSE, INFINITE, QS_ALLINPUT), WAIT_OBJECT_0);
    T_EQ(rt_msg_wait(CTX, 1, &unset, FALSE, INFINITE, QS_ALLINPUT), WAIT_OBJECT_0 + 1);
    T_EQ(rt_msg_get(CTX, &m, NULL, 0, 0, true), 1);
    T_EQ(rt_msg_wait(CTX, 1, &unset, FALSE, 0, QS_ALLINPUT), WAIT_TIMEOUT);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    HANDLE many[64], twice[3] = { unset, set, unset };
    for (unsigned i = 0; i < 64; i++) many[i] = set;
    STOPS(rt_msg_wait(CTX, 0, NULL, FALSE, 0, 0), "rt_msg_wait");
    STOPS(rt_msg_wait(CTX, 0, NULL, FALSE, 0, QS_SENDMESSAGE), "rt_msg_wait");
    STOPS(rt_msg_wait(CTX, 1, &set, TRUE, 0, QS_ALLINPUT), "rt_msg_wait");
    STOPS(rt_msg_wait(CTX, 64, many, FALSE, 0, QS_ALLINPUT), "rt_msg_wait");
    STOPS(rt_msg_wait(CTX, 1, NULL, FALSE, 0, QS_ALLINPUT), "rt_msg_wait");
    STOPS(rt_msg_wait(CTX, 3, twice, FALSE, 0, QS_ALLINPUT), "rt_msg_wait");

    T_CASE("a thread asleep in it is woken by a post from another thread");
    RtWndClass *c = cls(u"Q", 0);
    T_REQUIRE(c != NULL);
    static Worker worker;
    static Feed f;
    wu_start_one(&worker, 0, wait_then_get, &f);
    f.window = win_of(c, worker.t.id, NULL, 0);
    T_REQUIRE(f.window != NULL);
    wu_store(&f.ready, 1);
    T_TRUE(EVENTUALLY(wu_load(&f.ready) == 2));
    wu_settle();                                    /* asleep by now, or about to be: both must work */
    T_EQ(post(f.window, WM_USER, 5, 0), TRUE);
    T_REQUIRE(wu_finish(&worker, 1, "rt_msg_wait"));
    T_EQ(worker.result, WAIT_OBJECT_0);
    T_EQ(worker.n[0], WAIT_TIMEOUT);
}
