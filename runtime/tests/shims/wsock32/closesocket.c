#include "rt/test.h"
#include "rt/net.h"
#include "rt/thread.h"

/* Sockets are made and closed with the core's own calls (rt/net.h), not through another shim.
 * The first case runs before any WSAStartup, so it comes first: each test is its own process. */
TEST(wsock32, closesocket)
{
    WSADATA d;
    SOCKET s1, s2;

    T_CASE("before WSAStartup fails with WSANOTINITIALISED");
    T_EQ(CALL(wsock32, closesocket, (SOCKET)4), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);

    T_CASE("closes an open socket, with 0 and last error left alone");
    T_EQ(rt_net_startup(0x0202, &d), 0);
    s1 = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(s1 != INVALID_SOCKET);
    rt_thread_set_last_error(0x1234);
    T_EQ(CALL(wsock32, closesocket, s1), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("closing it again fails with WSAENOTSOCK");
    T_EQ(CALL(wsock32, closesocket, s1), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("0 and INVALID_SOCKET fail with WSAENOTSOCK");
    T_EQ(CALL(wsock32, closesocket, (SOCKET)0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(CALL(wsock32, closesocket, INVALID_SOCKET), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("a value with bit 32 set fails with WSAENOTSOCK and leaves the open socket open");
    s2 = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(s2 != INVALID_SOCKET);
    T_EQ(CALL(wsock32, closesocket, s2 | ((SOCKET)1 << 32)), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(CALL(wsock32, closesocket, s2), 0);        /* still open: this close succeeds */
}
