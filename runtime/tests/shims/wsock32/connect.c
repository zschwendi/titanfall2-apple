#include "rt/test.h"
#include "rt/net.h"

/* An IPv4 name: the address a.b.c.d and the port in network byte order (80 is 0x5000, 4000 is
 * 0xA00F, 0 is 0). */
static SOCKADDR_IN v4(BYTE a, BYTE b, BYTE c, BYTE d, USHORT port)
{
    SOCKADDR_IN n = { .sin_family = AF_INET, .sin_port = port };

    n.sin_addr.s_b[0] = a;
    n.sin_addr.s_b[1] = b;
    n.sin_addr.s_b[2] = c;
    n.sin_addr.s_b[3] = d;
    return n;
}

TEST(wsock32, connect)
{
    WSADATA d;
    SOCKET st, sd, sx;
    SOCKADDR_IN lo80 = v4(127, 0, 0, 1, 0x5000);
    SOCKADDR_IN far80 = v4(192, 0, 2, 1, 0x5000);
    SOCKADDR_IN any80 = v4(0, 0, 0, 0, 0x5000);
    SOCKADDR_IN lo4000 = v4(127, 0, 0, 1, 0xA00F);
    SOCKADDR_IN lo0 = v4(127, 0, 0, 1, 0);
    SOCKADDR_IN any4000 = v4(0, 0, 0, 0, 0xA00F);
    SOCKADDR_IN zero = { 0 };                               /* AF_UNSPEC, 16 zero bytes */
    SOCKADDR_IN got = { 0 };
    SOCKADDR_IN6 m6 = { 0 };
    SOCKET s6;
    ULONG nb;
    int gl, one;

    T_CASE("stream socket to 127.0.0.1 port 80 gives WSAECONNREFUSED, blocking and non-blocking");
    T_EQ(rt_net_startup(0x0202, &d), 0);
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, connect, st, (const SOCKADDR *)&lo80, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAECONNREFUSED);
    T_EQ(rt_net_close(st), 0);
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET);
    nb = 1;
    T_EQ(rt_net_ioctl(st, FIONBIO, &nb), 0);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, connect, st, (const SOCKADDR *)&lo80, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAECONNREFUSED);
    T_EQ(rt_net_close(st), 0);

    T_CASE("stream socket to 192.0.2.1 port 80 gives WSAENETUNREACH, blocking and non-blocking");
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, connect, st, (const SOCKADDR *)&far80, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENETUNREACH);
    T_EQ(rt_net_close(st), 0);
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET);
    nb = 1;
    T_EQ(rt_net_ioctl(st, FIONBIO, &nb), 0);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, connect, st, (const SOCKADDR *)&far80, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENETUNREACH);
    T_EQ(rt_net_close(st), 0);

    T_CASE("datagram socket to 127.0.0.1 port 4000 gives 0 and leaves last error alone, and getpeername reports it");
    sd = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(sd != INVALID_SOCKET);
    CALL(kernel32, SetLastError, 0xdead);
    T_EQ(CALL(wsock32, connect, sd, (const SOCKADDR *)&lo4000, 16), 0);
    T_EQ(T_LAST_ERROR(), 0xdead);
    gl = 16;
    T_EQ(rt_net_getpeername(sd, (SOCKADDR *)&got, &gl), 0);
    T_EQ(gl, 16);
    T_EQ(got.sin_family, AF_INET);
    T_EQ(got.sin_port, 0xA00F);
    T_EQ(got.sin_addr.s_b[0], 127);
    T_EQ(got.sin_addr.s_b[1], 0);
    T_EQ(got.sin_addr.s_b[2], 0);
    T_EQ(got.sin_addr.s_b[3], 1);

    T_CASE("connecting the datagram socket to a zeroed AF_UNSPEC name gives 0 and removes the peer");
    CALL(kernel32, SetLastError, 0xdead);
    T_EQ(CALL(wsock32, connect, sd, (const SOCKADDR *)&zero, 16), 0);
    T_EQ(T_LAST_ERROR(), 0xdead);
    T_EQ(rt_net_getpeername(sd, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);
    T_EQ(rt_net_close(sd), 0);

    T_CASE("stream socket to 0.0.0.0 port 80 gives WSAEADDRNOTAVAIL");
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, connect, st, (const SOCKADDR *)&any80, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEADDRNOTAVAIL);
    T_EQ(rt_net_close(st), 0);

    T_CASE("not a socket gets WSAENOTSOCK");
    sx = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(sx != INVALID_SOCKET);
    T_EQ(rt_net_close(sx), 0);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, connect, sx, (const SOCKADDR *)&lo4000, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    /* The core's stops (its comments say STOPS), after the checks of the last case.  Each
     * statement runs in a forked child, so this test goes on after each one. */
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET);
    T_TRAPS(CALL(wsock32, connect, st, (const SOCKADDR *)&zero, 16), "rt_net_connect: not implemented");
    sd = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(sd != INVALID_SOCKET);
    T_TRAPS(CALL(wsock32, connect, sd, (const SOCKADDR *)&zero, 15), "rt_net_connect: not implemented");
    T_TRAPS(CALL(wsock32, connect, sd, (const SOCKADDR *)&lo0, 16), "rt_net_connect: not implemented");
    T_TRAPS(CALL(wsock32, connect, sd, (const SOCKADDR *)&any4000, 16), "rt_net_connect: not implemented");

    s6 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s6 != INVALID_SOCKET);
    one = 1;
    T_EQ(rt_net_setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY, &one, 4), 0);
    m6.sin6_family = AF_INET6;
    m6.sin6_port = 0xA00F;                                  /* 4000 */
    m6.sin6_addr.s6_addr[10] = 0xff;                        /* ::ffff:127.0.0.1 */
    m6.sin6_addr.s6_addr[11] = 0xff;
    m6.sin6_addr.s6_addr[12] = 127;
    m6.sin6_addr.s6_addr[15] = 1;
    T_TRAPS(CALL(wsock32, connect, s6, (const SOCKADDR *)&m6, 28), "rt_net_connect: not implemented");

    T_EQ(rt_net_close(st), 0);
    T_EQ(rt_net_close(sd), 0);
    T_EQ(rt_net_close(s6), 0);
}
