#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/thread.h"

/* Options are read through the core's shim-visible entry point (CALL) with a sentinel in
 * every byte of an 8-byte buffer, so a write past the value is seen.  *optlen is preset to 4
 * (the four-byte options, or the one-byte options whose value is in the first byte). */
static int get_opt(SOCKET s, int level, int optname, unsigned char b[8], int *len)
{
    memset(b, 0xAA, 8);
    *len = 4;
    return CALL(wsock32, getsockopt, s, level, optname, (char *)b, len);
}

static DWORD word(const unsigned char b[8])
{
    DWORD v;
    memcpy(&v, b, 4);
    return v;
}

/* True if every byte from `from` to the end of the buffer still holds the sentinel. */
static int kept(const unsigned char b[8], int from)
{
    for (int i = from; i < 8; i++)
        if (b[i] != 0xAA) return 0;
    return 1;
}

TEST(wsock32, getsockopt)
{
    WSADATA d;
    SOCKET st, dg, s6;
    unsigned char b[8];
    int len;
    DWORD v;
    LINGER l;
    int r;

    T_EQ(rt_net_startup(0x0202, &d), 0);
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    dg = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    s6 = rt_net_socket(AF_INET6, SOCK_STREAM, 0);
    T_REQUIRE(st != INVALID_SOCKET && dg != INVALID_SOCKET && s6 != INVALID_SOCKET);

    T_CASE("defaults of a new socket: BOOLs 0, SO_LINGER {0, 0} on a stream socket, SO_SNDBUF and SO_RCVBUF 65536, SO_ERROR 0, SO_TYPE the socket's type");
    r = get_opt(st, SOL_SOCKET, SO_REUSEADDR, b, &len);
    T_EQ(r, 0);
    T_EQ(len, 4);
    T_EQ(word(b), 0);
    T_TRUE(kept(b, 4));
    r = get_opt(st, SOL_SOCKET, SO_BROADCAST, b, &len);
    T_EQ(r, 0);
    T_EQ(len, 4);
    T_EQ(word(b), 0);
    T_TRUE(kept(b, 4));
    memset(&l, 0xAA, sizeof l);
    len = 4;
    r = CALL(wsock32, getsockopt, st, SOL_SOCKET, SO_LINGER, (char *)&l, &len);
    T_EQ(r, 0);
    T_EQ(len, 4);
    T_EQ(l.l_onoff, 0);
    T_EQ(l.l_linger, 0);
    r = get_opt(st, SOL_SOCKET, SO_SNDBUF, b, &len);
    T_EQ(r, 0);
    T_EQ(len, 4);
    T_EQ(word(b), 65536);
    T_TRUE(kept(b, 4));
    r = get_opt(st, SOL_SOCKET, SO_RCVBUF, b, &len);
    T_EQ(r, 0);
    T_EQ(len, 4);
    T_EQ(word(b), 65536);
    T_TRUE(kept(b, 4));
    r = get_opt(st, SOL_SOCKET, SO_ERROR, b, &len);
    T_EQ(r, 0);
    T_EQ(len, 4);
    T_EQ(word(b), 0);
    T_TRUE(kept(b, 4));
    r = get_opt(st, SOL_SOCKET, SO_TYPE, b, &len);
    T_EQ(r, 0);
    T_EQ(len, 4);
    T_EQ(word(b), SOCK_STREAM);
    T_TRUE(kept(b, 4));
    r = get_opt(dg, SOL_SOCKET, SO_TYPE, b, &len);
    T_EQ(r, 0);
    T_EQ(word(b), SOCK_DGRAM);
    T_TRUE(kept(b, 4));

    T_CASE("one-byte options: TCP_NODELAY and IPV6_V6ONLY give one byte and leave the other three bytes");
    v = 0xdeadbeef;
    len = 4;
    r = CALL(wsock32, getsockopt, st, IPPROTO_TCP, TCP_NODELAY, (char *)&v, &len);
    T_EQ(r, 0);
    T_EQ(v, 0xdeadbe00);
    T_EQ(len, 1);
    v = 0xdeadbeef;
    len = 4;
    r = CALL(wsock32, getsockopt, s6, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&v, &len);
    T_EQ(r, 0);
    T_EQ(v, 0xdeadbe01);
    T_EQ(len, 1);

    T_CASE("a short optlen for a four-byte option, a NULL optval or optlen, or *optlen 0 gives WSAEFAULT");
    len = 3;
    r = CALL(wsock32, getsockopt, st, SOL_SOCKET, SO_RCVBUF, (char *)b, &len);
    T_EQ(r, SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    len = 4;
    r = CALL(wsock32, getsockopt, st, SOL_SOCKET, SO_RCVBUF, NULL, &len);
    T_EQ(r, SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    r = CALL(wsock32, getsockopt, st, SOL_SOCKET, SO_RCVBUF, (char *)b, NULL);
    T_EQ(r, SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    len = 0;
    r = CALL(wsock32, getsockopt, st, SOL_SOCKET, SO_RCVBUF, (char *)b, &len);
    T_EQ(r, SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_CASE("SO_LINGER on a datagram socket gives WSAENOPROTOOPT");
    len = 4;
    r = CALL(wsock32, getsockopt, dg, SOL_SOCKET, SO_LINGER, (char *)&l, &len);
    T_EQ(r, SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOPROTOOPT);

    T_CASE("not a socket gives WSAENOTSOCK");
    len = 4;
    r = get_opt(INVALID_SOCKET, SOL_SOCKET, SO_TYPE, b, &len);
    T_EQ(r, SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("a call that succeeds sets last error to 0");
    rt_thread_set_last_error(0xdead);
    r = get_opt(st, SOL_SOCKET, SO_TYPE, b, &len);
    T_EQ(r, 0);
    T_EQ(word(b), SOCK_STREAM);
    T_EQ(T_LAST_ERROR(), 0);
    rt_thread_set_last_error(10035);
    r = get_opt(st, SOL_SOCKET, SO_ERROR, b, &len);
    T_EQ(r, 0);
    T_EQ(word(b), 0);
    T_EQ(T_LAST_ERROR(), 0);
    rt_thread_set_last_error(10035);
    r = get_opt(st, SOL_SOCKET, SO_TYPE, b, &len);
    T_EQ(r, 0);
    T_EQ(word(b), SOCK_STREAM);
    T_EQ(T_LAST_ERROR(), 0);

    /* The stops of rt_net_getsockopt: each is reached, none is guarded by the shim. */
    T_TRAPS(get_opt(st, 0x1234, SO_TYPE, b, &len), "rt_net_getsockopt: not implemented");
    T_TRAPS(get_opt(st, SOL_SOCKET, 0x1234, b, &len), "rt_net_getsockopt: not implemented");
    len = -1;
    T_TRAPS(CALL(wsock32, getsockopt, st, SOL_SOCKET, SO_TYPE, (char *)b, &len), "rt_net_getsockopt: not implemented");
    T_TRAPS(get_opt(dg, IPPROTO_IPV6, IPV6_V6ONLY, b, &len), "rt_net_getsockopt: not implemented");

    T_EQ(rt_net_close(st), 0);
    T_EQ(rt_net_close(dg), 0);
    T_EQ(rt_net_close(s6), 0);
}
