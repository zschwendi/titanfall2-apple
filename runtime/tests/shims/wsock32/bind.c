#include "rt/test.h"
#include "rt/net.h"

/* A network-order USHORT as a host number (the runtime runs on little-endian hosts only). */
static unsigned host_port(USHORT net)
{
    return ((unsigned)(net & 0xff) << 8) | (net >> 8);
}

/* Ports are given in network byte order: 37015 is 0x9790, 4000 is 0xA00F, 0 is 0. */
TEST(wsock32, bind)
{
    WSADATA d;
    SOCKET s1, s2, s3, s4, s5, s6, s7;
    SOCKADDR_IN6 a6 = { .sin6_family = AF_INET6 };          /* :: */
    SOCKADDR_IN6 g6 = { 0 };
    SOCKADDR_IN a4 = { .sin_family = AF_INET, .sin_port = 0xA00F };
    SOCKADDR_IN6 bad = { .sin6_family = (SHORT)0xdead };
    unsigned nz = 0;
    int gl, one = 1;

    T_CASE("AF_INET6 datagram socket bound to :: port 37015 returns 0 and last error 0");
    T_EQ(rt_net_startup(0x0202, &d), 0);
    s1 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s1 != INVALID_SOCKET);
    a6.sin6_port = 0x9790;                                  /* 37015 */
    rt_set_last_error(T_CTX(), 0xdead);
    T_EQ(CALL(wsock32, bind, s1, (const SOCKADDR *)&a6, 28), 0);
    T_EQ(T_LAST_ERROR(), 0);
    gl = 28;
    T_EQ(rt_net_getsockname(s1, (SOCKADDR *)&g6, &gl), 0);
    T_EQ(gl, 28);
    T_EQ(g6.sin6_family, AF_INET6);
    T_EQ(g6.sin6_port, 0x9790);
    for (int i = 0; i < 16; i++) nz |= g6.sin6_addr.s6_addr[i];
    T_EQ(nz, 0);                                            /* still :: */

    T_CASE("port 0 takes a port of 49152 or above");
    s2 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s2 != INVALID_SOCKET);
    a6.sin6_port = 0;
    T_EQ(CALL(wsock32, bind, s2, (const SOCKADDR *)&a6, 28), 0);
    gl = 28;
    T_EQ(rt_net_getsockname(s2, (SOCKADDR *)&g6, &gl), 0);
    T_TRUE(host_port(g6.sin6_port) >= 49152);

    T_CASE("a second socket on the same port gets WSAEADDRINUSE");
    s3 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s3 != INVALID_SOCKET);
    a6.sin6_port = 0x9790;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, bind, s3, (const SOCKADDR *)&a6, 28), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEADDRINUSE);

    T_CASE("an address that is not local (192.0.2.1) gets WSAEADDRNOTAVAIL");
    s4 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(s4 != INVALID_SOCKET);
    a4.sin_addr.s_b[0] = 192;
    a4.sin_addr.s_b[1] = 0;
    a4.sin_addr.s_b[2] = 2;
    a4.sin_addr.s_b[3] = 1;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, bind, s4, (const SOCKADDR *)&a4, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEADDRNOTAVAIL);

    T_CASE("a second bind on a bound socket gets WSAEINVAL");
    a6.sin6_port = 0;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, bind, s1, (const SOCKADDR *)&a6, 28), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);

    T_CASE("namelen 27 for AF_INET6 gets WSAEFAULT");
    s5 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s5 != INVALID_SOCKET);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, bind, s5, (const SOCKADDR *)&a6, 27), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_CASE("name NULL gets WSAEFAULT");
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, bind, s5, NULL, 28), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_CASE("a wrong family gets WSAEAFNOSUPPORT");
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, bind, s5, (const SOCKADDR *)&bad, 28), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);

    T_CASE("not a socket gets WSAENOTSOCK");
    s6 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s6 != INVALID_SOCKET);
    T_EQ(rt_net_close(s6), 0);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, bind, s6, (const SOCKADDR *)&a6, 28), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    /* The core's stop (its comment says STOPS), after the checks of the last case: a socket
     * with SO_REUSEADDR set is bound to a port that is taken.  The statement runs in a forked
     * child, so this test goes on after it. */
    s7 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s7 != INVALID_SOCKET);
    T_EQ(rt_net_setsockopt(s7, SOL_SOCKET, SO_REUSEADDR, &one, 4), 0);
    a6.sin6_port = 0x9790;                                  /* held by s1 */
    T_TRAPS(CALL(wsock32, bind, s7, (const SOCKADDR *)&a6, 28), "rt_net_bind: not implemented");

    T_EQ(rt_net_close(s1), 0);
    T_EQ(rt_net_close(s2), 0);
    T_EQ(rt_net_close(s3), 0);
    T_EQ(rt_net_close(s4), 0);
    T_EQ(rt_net_close(s5), 0);
    T_EQ(rt_net_close(s7), 0);
}
