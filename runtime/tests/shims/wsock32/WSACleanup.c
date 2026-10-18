#include "rt/test.h"
#include "rt/net.h"

/* The start count is taken with rt_net_startup called directly: the shim of WSAStartup is
 * not part of this test.  Each case runs in order and the count carries from one to the next. */
TEST(wsock32, WSACleanup)
{
    WSADATA data;

    T_CASE("not started returns SOCKET_ERROR with WSANOTINITIALISED");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(wsock32, WSACleanup), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);

    T_CASE("two calls after two starts return 0 and set last error to 0");
    T_EQ(rt_net_startup(0x0202, &data), 0);
    T_EQ(rt_net_startup(0x0202, &data), 0);
    CALL(wsock32, WSASetLastError, (int)0xDEADBEEFu);
    T_EQ(CALL(wsock32, WSACleanup), 0);
    T_EQ(CALL(wsock32, WSAGetLastError), 0);    /* Wine ws2_32/tests/protocol.c, test_startup (review Z5) */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(wsock32, WSACleanup), 0);
    T_EQ(T_LAST_ERROR(), 0);

    T_CASE("a third call fails again with WSANOTINITIALISED");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(wsock32, WSACleanup), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);
}
