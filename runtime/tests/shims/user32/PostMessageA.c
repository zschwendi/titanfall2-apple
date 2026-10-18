#include "rt/test.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_msg_post.  A post to a real window, and to a window of
 * another thread, is proved in tests/core/win_queue.c. */
TEST(user32, PostMessageA)
{
    MSG m;

    T_CASE("a post with a NULL window is a thread message with the four values");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, PostMessageA, NULL, WM_USER + 3, 0x1122334455667788ull, -9), TRUE);
    T_EQ(rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_REMOVE, false), TRUE);
    T_TRUE(m.hwnd == NULL && m.message == WM_USER + 3 && m.wParam == 0x1122334455667788ull && m.lParam == -9);
    T_EQ(rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_REMOVE, false), FALSE);

    T_CASE("application and registered messages are posted as they are");
    UINT ok[] = { WM_NULL, WM_CLOSE, WM_QUIT, WM_KEYDOWN, WM_CHAR, 0x200, WM_USER, WM_APP, 0xC000, 0xFFFF };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        T_EQ(CALL(user32, PostMessageA, NULL, ok[i], 0x41, i), TRUE);
        T_EQ(rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_REMOVE, false), TRUE);
        T_TRUE(m.message == ok[i] && m.wParam == 0x41 && m.lParam == (LPARAM)i);
    }

    T_CASE("does not change last error on success");
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("what is not a window fails with ERROR_INVALID_WINDOW_HANDLE and queues nothing");
    T_EQ(CALL(user32, PostMessageA, (HWND)(uintptr_t)0x40000FF0, WM_USER, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, PostMessageA, (HWND)(uintptr_t)0xdeadbeef, WM_USER, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    T_EQ(rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_REMOVE, false), FALSE);

    /* RT-TRAPS: the core's stops, which the shim does not guard. */
    T_TRAPS(CALL(user32, PostMessageA, HWND_BROADCAST, WM_USER, 0, 0), "rt_msg_post: not implemented: HWND_BROADCAST");
    T_TRAPS(CALL(user32, PostMessageA, (HWND)(intptr_t)-1, WM_USER, 0, 0), "rt_msg_post: not implemented");
    T_TRAPS(CALL(user32, PostMessageA, RT_HWND_DESKTOP, WM_USER, 0, 0), "rt_msg_post: not implemented");
    T_TRAPS(CALL(user32, PostMessageA, NULL, WM_SETTEXT, 0, 0), "rt_msg_post: not implemented: system message 0xc");
    T_TRAPS(CALL(user32, PostMessageA, NULL, 0x10000, 0, 0), "rt_msg_post: not implemented");
    T_TRAPS(CALL(user32, PostMessageA, NULL, WM_CHAR, 0xE9, 0), "rt_msg_post: not implemented: character message");
}
