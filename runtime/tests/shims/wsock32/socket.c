#include "rt/test.h"
#include "rt/net.h"

/* The two sockets the engine makes at start, and the refusals of the core's checks, which
 * come in this order: type, family, protocol (Wine ws2_32/tests/sock.c, test_WSASocket, whose
 * table goes through WSASocketA; win-behaviour-check-5 Z4).  Each socket is a new handle: a
 * nonzero multiple of 4 below 0x80000000, and not the other. */
TEST(wsock32, socket)
{
    WSADATA d;
    SOCKET a, b, c, e;

    T_CASE("before WSAStartup gives INVALID_SOCKET and WSANOTINITIALISED");
    T_EQ(CALL(wsock32, socket, AF_INET6, SOCK_DGRAM, IPPROTO_UDP), INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);

    T_EQ(rt_net_startup(0x0202, &d), 0);

    T_CASE("AF_INET6 datagram and AF_INET datagram give two new sockets and set last error to 0");
    CALL(kernel32, SetLastError, 0x1234);
    a = CALL(wsock32, socket, AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    b = CALL(wsock32, socket, AF_INET, SOCK_DGRAM, 0);
    T_TRUE(a != 0 && a != INVALID_SOCKET);
    T_TRUE(b != 0 && b != INVALID_SOCKET);
    T_TRUE(a % 4 == 0 && b % 4 == 0);
    T_TRUE(a < 0x80000000u && b < 0x80000000u);
    T_TRUE(a != b);
    T_EQ(T_LAST_ERROR(), 0);                    /* Wine ws2_32/tests/sock.c, test_WSASocket */

    T_CASE("socket type 0 with IPPROTO_UDP succeeds and sets last error to 0");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    c = CALL(wsock32, socket, AF_INET, 0, IPPROTO_UDP);
    T_TRUE(c != 0 && c != INVALID_SOCKET);
    T_TRUE(c % 4 == 0 && c < 0x80000000u && c != a && c != b);
    T_EQ(T_LAST_ERROR(), 0);                    /* Wine sock.c, test_WSASocket: every successful row */

    T_CASE("address family other than AF_UNSPEC, AF_INET or AF_INET6 gives WSAEAFNOSUPPORT");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, socket, 0xdead, SOCK_DGRAM, 0), INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, socket, 0xdead, SOCK_STREAM, 0xdead), INVALID_SOCKET);   /* family before protocol */
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);

    T_CASE("socket type other than 0, SOCK_STREAM or SOCK_DGRAM gives WSAESOCKTNOSUPPORT");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, socket, AF_INET, 0xdead, 0), INVALID_SOCKET);   /* not SOCK_RAW (3): Wine sock.c, test_WSASocket, has it succeed or give WSAEACCES */
    T_EQ(T_LAST_ERROR(), WSAESOCKTNOSUPPORT);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, socket, 0xdead, 0xdead, IPPROTO_TCP), INVALID_SOCKET);   /* type before family */
    T_EQ(T_LAST_ERROR(), WSAESOCKTNOSUPPORT);

    T_CASE("protocol that does not go with the type gives WSAEPROTONOSUPPORT");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, socket, AF_INET, SOCK_DGRAM, IPPROTO_TCP), INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEPROTONOSUPPORT);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, socket, AF_INET, SOCK_STREAM, IPPROTO_UDP), INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEPROTONOSUPPORT);

    T_CASE("AF_UNSPEC with protocol 0 gives WSAEINVAL");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, socket, AF_UNSPEC, SOCK_DGRAM, 0), INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);

    T_CASE("AF_UNSPEC with IPPROTO_UDP gives a socket");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    e = CALL(wsock32, socket, AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);
    T_TRUE(e != 0 && e != INVALID_SOCKET);
    T_TRUE(e % 4 == 0 && e < 0x80000000u && e != a && e != b && e != c);
    T_EQ(T_LAST_ERROR(), 0);

    T_EQ(rt_net_close(a), 0);
    T_EQ(rt_net_close(b), 0);
    T_EQ(rt_net_close(c), 0);
    T_EQ(rt_net_close(e), 0);
}
