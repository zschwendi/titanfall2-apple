#include <string.h>

#include "rt/test.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_msg_peek (each statement there names its source).  The
 * window filter with real windows is proved in tests/core/win_queue.c. */
static bool all_a5(const MSG *m) {
    for (size_t i = 0; i < sizeof *m; i++) if (((const uint8_t *)m)[i] != 0xA5) return false;
    return true;
}

TEST(user32, PeekMessageA)
{
    MSG m;

    T_CASE("FALSE at once on an empty queue, the MSG untouched");
    CALL(kernel32, SetLastError, 0x1234);
    for (unsigned i = 0; i < 1000; i++) {
        memset(&m, 0xA5, sizeof m);
        T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, i % 2 ? PM_REMOVE : PM_NOREMOVE), FALSE);
        T_TRUE(all_a5(&m));
    }
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, PM_REMOVE | PM_NOYIELD), FALSE);

    T_CASE("PM_NOREMOVE sees a posted message and leaves it");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 7, 1, 2), TRUE);
    for (int i = 0; i < 2; i++) {
        memset(&m, 0xA5, sizeof m);
        T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, PM_NOREMOVE), TRUE);
        T_TRUE(m.hwnd == NULL && m.message == WM_USER + 7 && m.wParam == 1 && m.lParam == 2);
    }

    T_CASE("PM_REMOVE takes it");
    memset(&m, 0xA5, sizeof m);
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.hwnd == NULL && m.message == WM_USER + 7 && m.wParam == 1 && m.lParam == 2);
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, PM_REMOVE), FALSE);

    T_CASE("a range that excludes the message");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 7, 3, 4), TRUE);
    memset(&m, 0xA5, sizeof m);
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, WM_USER + 8, WM_USER + 9, PM_REMOVE), FALSE);
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, WM_USER, WM_USER + 6, PM_REMOVE), FALSE);
    T_TRUE(all_a5(&m));
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, WM_USER + 7, WM_USER + 7, PM_REMOVE), TRUE);
    T_TRUE(m.wParam == 3 && m.lParam == 4);

    T_CASE("messages come out in the order posted");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 1, 0, 0), TRUE);
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 2, 0, 0), TRUE);
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_EQ(m.message, WM_USER + 1);
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, PM_REMOVE), TRUE);
    T_EQ(m.message, WM_USER + 2);

    T_CASE("(HWND)-1 takes a thread message");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 3, 0, 0), TRUE);
    T_EQ(CALL(user32, PeekMessageA, &m, (HWND)(intptr_t)-1, 0, 0, PM_REMOVE), TRUE);
    T_TRUE(m.hwnd == NULL && m.message == WM_USER + 3);

    T_CASE("does not change last error on success or on an empty queue");
    T_EQ(CALL(user32, PeekMessageA, &m, NULL, 0, 0, PM_REMOVE), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a filter that is not a window fails with ERROR_INVALID_WINDOW_HANDLE");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER, 0, 0), TRUE);
    memset(&m, 0xA5, sizeof m);
    T_EQ(CALL(user32, PeekMessageA, &m, (HWND)(uintptr_t)0x40000FF0, 0, 0, PM_REMOVE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_TRUE(all_a5(&m));

    /* RT-TRAPS: the core's stops, which the shim does not guard.  One more is not in RT-TRAPS
     * because a shim test cannot reach it yet: a window of another thread as the filter
     * (it needs a window; tests/core/win_queue.c has it). */
    T_TRAPS(CALL(user32, PeekMessageA, NULL, NULL, 0, 0, PM_REMOVE), "rt_msg_peek: not implemented");
    T_TRAPS(CALL(user32, PeekMessageA, &m, NULL, 0, 0, 4), "rt_msg_peek: not implemented");
    T_TRAPS(CALL(user32, PeekMessageA, &m, (HWND)(uintptr_t)1, 0, 0, PM_REMOVE), "rt_msg_peek: not implemented");
    T_TRAPS(CALL(user32, PeekMessageA, &m, (HWND)(uintptr_t)0xFFFF, 0, 0, PM_REMOVE), "rt_msg_peek: not implemented");
    T_TRAPS(CALL(user32, PeekMessageA, &m, NULL, 0x10000, 0x10001, PM_REMOVE), "rt_msg_peek: not implemented");
    T_TRAPS(CALL(user32, PeekMessageA, &m, NULL, 9, 8, PM_REMOVE), "rt_msg_peek: not implemented");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_QUIT, 0, 0), TRUE);
    T_TRAPS(CALL(user32, PeekMessageA, &m, NULL, WM_USER + 1, WM_USER + 2, PM_REMOVE), "rt_msg_peek: not implemented");
}
