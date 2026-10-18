#include <string.h>

#include "rt/test.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_msg_get.  Every call here finds a message or fails for its
 * filter: on an empty queue the function sleeps for ever (tests/core/win_queue.c proves the
 * sleep and the wake with a second thread). */
TEST(user32, GetMessageA)
{
    MSG m;

    T_CASE("a posted message returns 1 with its values");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 4, 5, -6), TRUE);
    memset(&m, 0xA5, sizeof m);
    T_EQ(CALL(user32, GetMessageA, &m, NULL, 0, 0), 1);
    T_TRUE(m.hwnd == NULL && m.message == WM_USER + 4 && m.wParam == 5 && m.lParam == -6);
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 1, 0, 0), TRUE);
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 2, 0, 0), TRUE);
    T_EQ(CALL(user32, GetMessageA, &m, (HWND)(intptr_t)-1, WM_USER + 2, WM_USER + 2), 1);
    T_EQ(m.message, WM_USER + 2);
    T_EQ(CALL(user32, GetMessageA, &m, NULL, 0, 0), 1);
    T_EQ(m.message, WM_USER + 1);

    T_CASE("a posted WM_QUIT returns 0");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_QUIT, 0xbeef, 0), TRUE);
    T_EQ(CALL(user32, GetMessageA, &m, NULL, 0, 0), 0);
    T_TRUE(m.hwnd == NULL && m.message == WM_QUIT && m.wParam == 0xbeef);

    T_CASE("does not change last error on success");
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a filter that is not a window returns -1 with ERROR_INVALID_WINDOW_HANDLE");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER, 0, 0), TRUE);
    memset(&m, 0xA5, sizeof m);
    T_EQ(CALL(user32, GetMessageA, &m, (HWND)(uintptr_t)0x40000FF0, 0, 0), -1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(m.message, 0xA5A5A5A5);

    /* RT-TRAPS: the core's stops.  One more is not in RT-TRAPS because a shim test cannot
     * reach it yet: a window of another thread as the filter (tests/core/win_queue.c has it). */
    T_TRAPS(CALL(user32, GetMessageA, NULL, NULL, 0, 0), "rt_msg_get: not implemented");
    T_TRAPS(CALL(user32, GetMessageA, &m, (HWND)(uintptr_t)1, 0, 0), "rt_msg_get: not implemented");
    T_TRAPS(CALL(user32, GetMessageA, &m, (HWND)(uintptr_t)0xFFFF, 0, 0), "rt_msg_get: not implemented");
    T_TRAPS(CALL(user32, GetMessageA, &m, NULL, 0x10000, 0x10001), "rt_msg_get: not implemented");
    T_TRAPS(CALL(user32, GetMessageA, &m, NULL, 9, 8), "rt_msg_get: not implemented");
    T_EQ(CALL(user32, GetMessageA, &m, NULL, 0, 0), 1);
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_QUIT, 0, 0), TRUE);
    T_TRAPS(CALL(user32, GetMessageA, &m, NULL, WM_USER + 1, WM_USER + 2), "rt_msg_get: not implemented");
}
