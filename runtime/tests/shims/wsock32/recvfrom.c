#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/thread.h"

/* A buffer for the sender's address, big enough for either family (28 bytes). */
typedef union { SOCKADDR_IN in; SOCKADDR_IN6 in6; } ADDRBUF;

/* 127.0.0.1, port 0: what bind gets. */
static void loopback(SOCKADDR_IN *a)
{
    memset(a, 0, sizeof *a);
    a->sin_family = (SHORT)AF_INET;
    a->sin_addr.s_b[0] = 127;
    a->sin_addr.s_b[3] = 1;
}

TEST(wsock32, recvfrom)
{
    WSADATA d;
    SOCKADDR_IN lo, ra, sa;
    ADDRBUF from;
    SOCKET r, s, b2, st, u, c;
    ULONG on = 1;
    unsigned char pat[28];
    char buf[64];
    char msg[17] = "0123456789abcdef";
    char longmsg[33] = "abcdefghijklmnopqrstuvwxyz012345";
    int fromlen, rl = 16, sl = 16, i;

    T_EQ(rt_net_startup(0x0202, &d), 0);
    r = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    s = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    b2 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(r != INVALID_SOCKET && s != INVALID_SOCKET && b2 != INVALID_SOCKET);
    loopback(&lo);
    T_EQ(rt_net_bind(r, (const SOCKADDR *)&lo, (int)sizeof lo), 0);
    T_EQ(rt_net_bind(s, (const SOCKADDR *)&lo, (int)sizeof lo), 0);
    T_EQ(rt_net_bind(b2, (const SOCKADDR *)&lo, (int)sizeof lo), 0);
    T_EQ(rt_net_getsockname(r, (SOCKADDR *)&ra, &rl), 0);
    T_EQ(rt_net_getsockname(s, (SOCKADDR *)&sa, &sl), 0);
    T_EQ(rt_net_ioctl(r, FIONBIO, &on), 0);

    T_CASE("a bound non-blocking socket with nothing queued gives WSAEWOULDBLOCK ten times in a row, leaving from and fromlen untouched");
    memset(pat, 0x5a, sizeof pat);
    for (i = 0; i < 10; i++) {
        memset(&from, 0x5a, sizeof from);
        fromlen = 28;
        T_EQ(CALL(wsock32, recvfrom, r, buf, 64, 0, (SOCKADDR *)&from, &fromlen), SOCKET_ERROR);
        T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);
        T_EQ(fromlen, 28);
        T_EQ(memcmp(&from, pat, sizeof from), 0);
    }

    T_CASE("a queued datagram comes back with its bytes, its sender's address and port, and fromlen 16");
    T_EQ(rt_net_sendto(s, msg, 16, 0, (const SOCKADDR *)&ra, 16), 16);
    memset(&from, 0, sizeof from);
    fromlen = 28;
    T_EQ(CALL(wsock32, recvfrom, r, buf, 64, 0, (SOCKADDR *)&from, &fromlen), 16);
    T_EQ(memcmp(buf, msg, 16), 0);
    T_EQ(fromlen, 16);
    T_EQ(from.in.sin_family, AF_INET);
    T_EQ(from.in.sin_addr.s_b[0], 127);
    T_EQ(from.in.sin_addr.s_b[3], 1);
    T_EQ(from.in.sin_port, sa.sin_port);

    T_CASE("a call that succeeds sets last error to 0");
    T_EQ(rt_net_sendto(s, msg, 16, 0, (const SOCKADDR *)&ra, 16), 16);
    rt_thread_set_last_error(0xdead);
    T_EQ(CALL(wsock32, recvfrom, r, buf, 64, 0, NULL, NULL), 16);
    T_EQ(T_LAST_ERROR(), 0);

    T_CASE("a datagram longer than len gives WSAEMSGSIZE, and the next call WSAEWOULDBLOCK");
    T_EQ(rt_net_sendto(s, longmsg, 32, 0, (const SOCKADDR *)&ra, 16), 32);
    memset(&from, 0x5a, sizeof from);
    fromlen = 28;
    T_EQ(CALL(wsock32, recvfrom, r, buf, 16, 0, (SOCKADDR *)&from, &fromlen), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEMSGSIZE);
    T_EQ(memcmp(buf, longmsg, 16), 0);
    T_EQ(fromlen, 16);                  /* from and fromlen are written with WSAEMSGSIZE too */
    T_EQ(from.in.sin_family, AF_INET);
    T_EQ(from.in.sin_addr.s_b[0], 127);
    T_EQ(from.in.sin_addr.s_b[3], 1);
    T_EQ(from.in.sin_port, sa.sin_port);
    T_EQ(CALL(wsock32, recvfrom, r, buf, 16, 0, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);

    T_CASE("an unbound socket gives WSAEINVAL");
    u = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(u != INVALID_SOCKET);
    T_EQ(rt_net_ioctl(u, FIONBIO, &on), 0);
    T_EQ(CALL(wsock32, recvfrom, u, buf, 64, 0, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);

    T_CASE("a stream socket that is not connected gives WSAENOTCONN");
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET);
    T_EQ(rt_net_ioctl(st, FIONBIO, &on), 0);
    T_EQ(CALL(wsock32, recvfrom, st, buf, 64, 0, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);

    T_CASE("not a socket gives WSAENOTSOCK");
    c = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(c != INVALID_SOCKET);
    T_EQ(rt_net_close(c), 0);
    T_EQ(CALL(wsock32, recvfrom, c, buf, 64, 0, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(CALL(wsock32, recvfrom, INVALID_SOCKET, buf, 64, 0, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    /* The core's stops (rt/net.h), each proved in a forked child after the last case. */
    T_TRAPS(CALL(wsock32, recvfrom, r, buf, 64, MSG_PEEK, NULL, NULL), "rt_net_recvfrom: not implemented");
    T_TRAPS(CALL(wsock32, recvfrom, b2, buf, 64, 0, NULL, NULL), "rt_net_recvfrom: not implemented");

    T_EQ(rt_net_close(r), 0);
    T_EQ(rt_net_close(s), 0);
    T_EQ(rt_net_close(b2), 0);
    T_EQ(rt_net_close(st), 0);
    T_EQ(rt_net_close(u), 0);
}
