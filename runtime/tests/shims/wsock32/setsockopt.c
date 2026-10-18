#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/thread.h"

/* Options are read back with the core's rt_net_getsockopt (rt/net.h), not through another
 * shim.  v is preset to 0xdeadbeef with *optlen 4: a one-byte option leaves the upper three
 * bytes and gives *optlen 1 (rt/net.h, getsockopt). */
static int readback(SOCKET s, int level, int optname, DWORD *v, int *optlen)
{
    *v = 0xdeadbeef;
    *optlen = 4;
    return rt_net_getsockopt(s, level, optname, v, optlen);
}

/* 127.0.0.1, port 0: what bind gets. */
static void loopback(SOCKADDR_IN *a)
{
    memset(a, 0, sizeof *a);
    a->sin_family = (SHORT)AF_INET;
    a->sin_addr.s_b[0] = 127;
    a->sin_addr.s_b[3] = 1;
}

TEST(wsock32, setsockopt)
{
    WSADATA d;
    SOCKADDR_IN lo;
    SOCKET u6, u4, t, bd, c;
    LINGER lg;
    unsigned char b1 = 1;
    int one = 1, zero = 0, big = 0x100, neg = -1, val, len, llen;
    DWORD v;

    T_EQ(rt_net_startup(0x0202, &d), 0);
    u6 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    u4 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    t = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    bd = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(u6 != INVALID_SOCKET && u4 != INVALID_SOCKET && t != INVALID_SOCKET && bd != INVALID_SOCKET);
    loopback(&lo);
    T_EQ(rt_net_bind(bd, (const SOCKADDR *)&lo, (int)sizeof lo), 0);

    T_CASE("the engine's calls on a UDP socket read back as set");
    val = 0x20000;
    T_EQ(CALL(wsock32, setsockopt, u6, SOL_SOCKET, SO_SNDBUF, &val, 4), 0);
    T_EQ(CALL(wsock32, setsockopt, u6, SOL_SOCKET, SO_RCVBUF, &val, 4), 0);
    T_EQ(CALL(wsock32, setsockopt, u6, IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4), 0);
    T_EQ(CALL(wsock32, setsockopt, u6, SOL_SOCKET, SO_REUSEADDR, &one, 4), 0);
    T_EQ(readback(u6, SOL_SOCKET, SO_SNDBUF, &v, &len), 0);
    T_EQ(v, 0x20000u);
    T_EQ(len, 4);
    T_EQ(readback(u6, SOL_SOCKET, SO_RCVBUF, &v, &len), 0);
    T_EQ(v, 0x20000u);
    T_EQ(len, 4);
    T_EQ(readback(u6, IPPROTO_IPV6, IPV6_V6ONLY, &v, &len), 0);
    T_EQ(v, 0xdeadbe00u);
    T_EQ(len, 1);
    T_EQ(readback(u6, SOL_SOCKET, SO_REUSEADDR, &v, &len), 0);
    T_EQ(v, 1u);
    T_EQ(len, 4);

    T_CASE("SO_KEEPALIVE, SO_LINGER and TCP_NODELAY on a TCP socket read back as set");
    val = 1;
    T_EQ(CALL(wsock32, setsockopt, t, SOL_SOCKET, SO_KEEPALIVE, &val, 4), 0);
    lg.l_onoff = 0;
    lg.l_linger = 0;
    T_EQ(CALL(wsock32, setsockopt, t, SOL_SOCKET, SO_LINGER, &lg, (int)sizeof lg), 0);
    T_EQ(CALL(wsock32, setsockopt, t, IPPROTO_TCP, TCP_NODELAY, &val, 4), 0);
    T_EQ(readback(t, SOL_SOCKET, SO_KEEPALIVE, &v, &len), 0);
    T_EQ(v, 0xdeadbe01u);
    T_EQ(len, 1);
    lg.l_onoff = 9;
    lg.l_linger = 9;
    llen = (int)sizeof lg;
    T_EQ(rt_net_getsockopt(t, SOL_SOCKET, SO_LINGER, &lg, &llen), 0);
    T_EQ(lg.l_onoff, 0);
    T_EQ(lg.l_linger, 0);
    T_EQ(llen, 4);
    T_EQ(readback(t, IPPROTO_TCP, TCP_NODELAY, &v, &len), 0);
    T_EQ(v, 0xdeadbe01u);
    T_EQ(len, 1);

    T_CASE("TCP_NODELAY on a datagram socket gives WSAENOPROTOOPT");
    T_EQ(CALL(wsock32, setsockopt, u4, IPPROTO_TCP, TCP_NODELAY, &one, 4), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOPROTOOPT);

    T_CASE("an optlen too small gives WSAEFAULT");
    T_EQ(CALL(wsock32, setsockopt, t, SOL_SOCKET, SO_SNDBUF, &val, 3), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(CALL(wsock32, setsockopt, t, SOL_SOCKET, SO_KEEPALIVE, &val, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(CALL(wsock32, setsockopt, t, SOL_SOCKET, SO_LINGER, &lg, 3), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_CASE("SO_REUSEADDR with optlen 1 gives 0 and reads back 1");
    T_EQ(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_REUSEADDR, &b1, 1), 0);
    T_EQ(readback(u4, SOL_SOCKET, SO_REUSEADDR, &v, &len), 0);
    T_EQ(v, 1u);
    T_EQ(len, 4);

    T_CASE("TCP_NODELAY keeps only the first byte of its value");
    T_EQ(CALL(wsock32, setsockopt, t, IPPROTO_TCP, TCP_NODELAY, &big, 4), 0);
    T_EQ(readback(t, IPPROTO_TCP, TCP_NODELAY, &v, &len), 0);
    T_EQ(v, 0xdeadbe00u);
    T_EQ(len, 1);

    T_CASE("IPV6_V6ONLY 0 on an AF_INET datagram socket gives 0");
    T_EQ(CALL(wsock32, setsockopt, u4, IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4), 0);
    T_EQ(readback(u4, IPPROTO_IPV6, IPV6_V6ONLY, &v, &len), 0);
    T_EQ(v, 0xdeadbe00u);
    T_EQ(len, 1);

    T_CASE("not a socket gives WSAENOTSOCK");
    c = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(c != INVALID_SOCKET);
    T_EQ(rt_net_close(c), 0);
    T_EQ(CALL(wsock32, setsockopt, c, SOL_SOCKET, SO_REUSEADDR, &one, 4), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(CALL(wsock32, setsockopt, INVALID_SOCKET, SOL_SOCKET, SO_REUSEADDR, &one, 4), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("a call that succeeds sets last error to 0");
    rt_thread_set_last_error(0xdead);
    T_EQ(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_REUSEADDR, &one, 4), 0);
    T_EQ(T_LAST_ERROR(), 0);

    /* The core's stops (rt/net.h), each proved in a forked child after the last case. */
    T_TRAPS(CALL(wsock32, setsockopt, u4, SOL_SOCKET, 0x7777, &one, 4), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, u4, 0x1234, SO_REUSEADDR, &one, 4), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_LINGER, &lg, 4), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_REUSEADDR, &one, neg), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_SNDBUF, &neg, 4), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, bd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_REUSEADDR, &one, 0), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_BROADCAST, &one, 0), "rt_net_setsockopt: not implemented");
    T_TRAPS(CALL(wsock32, setsockopt, u4, SOL_SOCKET, SO_ERROR, &one, 4), "rt_net_setsockopt: not implemented");

    T_EQ(rt_net_close(u6), 0);
    T_EQ(rt_net_close(u4), 0);
    T_EQ(rt_net_close(t), 0);
    T_EQ(rt_net_close(bd), 0);
}
