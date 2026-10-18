#include <unistd.h>

#include "rt/test.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_msg_time. */
TEST(user32, GetMessageTime)
{
    MSG m;

    T_CASE("0 before the thread has taken a message");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetMessageTime), 0);

    T_CASE("a message looked at and left does not count");
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER, 0, 0), TRUE);
    T_EQ(rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_NOREMOVE, false), TRUE);
    T_EQ(CALL(user32, GetMessageTime), 0);

    T_CASE("the time of the last message taken");
    T_EQ(rt_msg_peek(T_CTX(), &m, NULL, 0, 0, PM_REMOVE, false), TRUE);
    T_EQ(CALL(user32, GetMessageTime), (LONG)m.time);
    LONG first = (LONG)m.time;
    usleep(5000);
    T_EQ(rt_msg_post(T_CTX(), NULL, WM_USER + 1, 0, 0), TRUE);
    T_EQ(CALL(user32, GetMessageTime), first);
    T_EQ(rt_msg_get(T_CTX(), &m, NULL, 0, 0, false), 1);
    T_EQ(CALL(user32, GetMessageTime), (LONG)m.time);
    T_TRUE((LONG)m.time - first >= 4);

    T_CASE("does not change last error");
    T_EQ(T_LAST_ERROR(), 0x1234);
}
