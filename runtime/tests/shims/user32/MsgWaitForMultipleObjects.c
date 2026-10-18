#include "rt/test.h"
#include "rt/sync.h"
#include "rt/time.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_msg_wait.  The wake of a sleeping thread by a post from
 * another thread is proved in tests/core/win_queue.c. */
TEST(user32, MsgWaitForMultipleObjects)
{
    MSG m;
    HANDLE set = rt_event_create(T_CTX(), TRUE, TRUE, NULL), unset = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
    T_REQUIRE(set && unset);
    HANDLE two[2] = { unset, set }, bad[2] = { set, (HANDLE)(uintptr_t)0x7654 }, twice[2] = { set, set };

    T_CASE("no handle and nobody posts gives WAIT_TIMEOUT after the time limit");
    CALL(kernel32, SetLastError, 0x1234);
    uint64_t t0 = rt_time_mono_100ns();
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 0, NULL, FALSE, 20, QS_ALLINPUT), WAIT_TIMEOUT);
    T_TRUE(rt_time_mono_100ns() - t0 >= 200000);
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 1, &unset, FALSE, 0, QS_ALLINPUT), WAIT_TIMEOUT);

    T_CASE("a set event gives its index");
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 1, &set, FALSE, INFINITE, QS_ALLINPUT), WAIT_OBJECT_0);
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 2, two, FALSE, INFINITE, QS_POSTMESSAGE), WAIT_OBJECT_0 + 1);

    T_CASE("a posted message gives WAIT_OBJECT_0 + nCount");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER, 0, 0), TRUE);
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 1, &unset, FALSE, INFINITE, QS_ALLINPUT), WAIT_OBJECT_0 + 1);

    T_CASE("after the thread has looked at its queue the message is no longer new");
    T_EQ(rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_NOREMOVE, false), TRUE);
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 1, &unset, FALSE, 20, QS_ALLINPUT), WAIT_TIMEOUT);
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 0, NULL, FALSE, 0, QS_ALLEVENTS), WAIT_TIMEOUT);

    T_CASE("does not change last error on success");
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle that is not open gives WAIT_FAILED with ERROR_INVALID_HANDLE");
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 2, bad, FALSE, 0, QS_ALLINPUT), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* RT-TRAPS: the core's stops, which the shim does not guard. */
    HANDLE many[64];
    for (unsigned i = 0; i < 64; i++) many[i] = set;
    T_TRAPS(CALL(user32, MsgWaitForMultipleObjects, 64, many, FALSE, 0, QS_ALLINPUT), "rt_msg_wait: not implemented: 64 handles");
    T_TRAPS(CALL(user32, MsgWaitForMultipleObjects, 0, NULL, FALSE, 0, 0), "rt_msg_wait: not implemented: wake mask 0x0");
    T_TRAPS(CALL(user32, MsgWaitForMultipleObjects, 1, &set, TRUE, 0, QS_ALLINPUT), "rt_msg_wait: not implemented");
    T_TRAPS(CALL(user32, MsgWaitForMultipleObjects, 1, NULL, FALSE, 0, QS_ALLINPUT), "rt_msg_wait: not implemented");
    T_TRAPS(CALL(user32, MsgWaitForMultipleObjects, 2, twice, FALSE, 0, QS_ALLINPUT), "rt_msg_wait: not implemented");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER, 0, 0), TRUE);
    T_EQ(CALL(user32, MsgWaitForMultipleObjects, 0, NULL, FALSE, INFINITE, QS_ALLINPUT), WAIT_OBJECT_0);
    T_TRAPS(CALL(user32, MsgWaitForMultipleObjects, 0, NULL, FALSE, 0, QS_ALLINPUT), "rt_msg_wait: not implemented: a second wait");
}
