#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/thread.h"

/* 127.0.0.1, port 4000 (0xA00F in network byte order): a destination for the send that the
 * shut-down socket must refuse. */
static void dest(SOCKADDR_IN *a)
{
    memset(a, 0, sizeof *a);
    a->sin_family = (SHORT)AF_INET;
    a->sin_port = 0xA00F;
    a->sin_addr.s_b[0] = 127;
    a->sin_addr.s_b[3] = 1;
}

TEST(wsock32, shutdown)
{
    WSADATA d;
    SOCKADDR_IN to;
    SOCKET st, u, g, c;

    T_EQ(rt_net_startup(0x0202, &d), 0);
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    u = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(st != INVALID_SOCKET && u != INVALID_SOCKET);
    dest(&to);

    T_CASE("a stream socket gets WSAENOTCONN");
    T_EQ(CALL(wsock32, shutdown, st, SD_BOTH), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);

    T_CASE("how 3 gets WSAEINVAL");
    T_EQ(CALL(wsock32, shutdown, u, 3), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);

    T_CASE("a datagram socket with SD_SEND gives 0 and a following rt_net_sendto gets WSAESHUTDOWN");
    T_EQ(CALL(wsock32, shutdown, u, SD_SEND), 0);
    T_EQ(rt_net_sendto(u, "x", 1, 0, (const SOCKADDR *)&to, (int)sizeof to), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAESHUTDOWN);

    T_CASE("not a socket gives WSAENOTSOCK");
    c = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(c != INVALID_SOCKET);
    T_EQ(rt_net_close(c), 0);
    T_EQ(CALL(wsock32, shutdown, c, SD_BOTH), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("a call that succeeds sets last error to 0");
    g = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(g != INVALID_SOCKET);
    rt_thread_set_last_error(0xdead);
    T_EQ(CALL(wsock32, shutdown, g, SD_RECEIVE), 0);
    T_EQ(T_LAST_ERROR(), 0);

    T_EQ(rt_net_close(g), 0);
    T_EQ(rt_net_close(u), 0);
    T_EQ(rt_net_close(st), 0);
}
