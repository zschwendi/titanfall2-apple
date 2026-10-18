#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/win/net.h"

/* Port 4000 in network byte order, written by hand. */
#define PORT_4000  ((USHORT)0xA00F)

static void addr_loopback(SOCKADDR_IN *a, USHORT port)
{
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET;
    a->sin_port = port;
    a->sin_addr.s_b[0] = 127;
    a->sin_addr.s_b[3] = 1;
}

/* A bound, non-blocking AF_INET datagram socket on 127.0.0.1:4000. */
static SOCKET receiver(void)
{
    SOCKADDR_IN a;
    ULONG one = 1;
    SOCKET s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_loopback(&a, PORT_4000);
    T_EQ(rt_net_bind(s, (SOCKADDR *)&a, sizeof a), 0);
    T_EQ(rt_net_ioctl(s, FIONBIO, &one), 0);
    return s;
}

/* Queues one datagram of n bytes from a fresh unbound socket to 127.0.0.1:4000. */
static void send_to_receiver(const char *data, int n)
{
    SOCKADDR_IN to;
    SOCKET snd = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_loopback(&to, PORT_4000);
    T_EQ(rt_net_sendto(snd, data, n, 0, (SOCKADDR *)&to, sizeof to), n);
    rt_net_close(snd);
}

TEST(wsock32, recv)
{
    WSADATA d;
    SOCKET s, r, blk;
    SOCKADDR_IN a;
    char buf[64];
    char msg[32];
    T_EQ(rt_net_startup(0x0202, &d), 0);

    T_CASE("stream socket gets WSAENOTCONN");
    s = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_EQ(CALL(wsock32, recv, s, buf, 16, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);
    rt_net_close(s);

    T_CASE("not a socket gets WSAENOTSOCK");
    T_EQ(CALL(wsock32, recv, (SOCKET)12345, buf, 16, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("unbound datagram socket gets WSAEINVAL");
    s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_EQ(CALL(wsock32, recv, s, buf, 16, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);
    rt_net_close(s);

    T_CASE("bound non-blocking datagram socket with nothing queued gets WSAEWOULDBLOCK");
    r = receiver();
    T_EQ(CALL(wsock32, recv, r, buf, 16, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);

    T_CASE("a queued datagram comes back with its length");
    memcpy(msg, "hello, datagram!", 16);
    send_to_receiver(msg, 16);
    memset(buf, 'x', sizeof buf);
    T_EQ(CALL(wsock32, recv, r, buf, 64, 0), 16);
    T_TRUE(memcmp(buf, msg, 16) == 0);
    T_TRUE(buf[16] == 'x' && buf[63] == 'x');       /* nothing written past the datagram */
    T_EQ(T_LAST_ERROR(), 0);

    T_CASE("a datagram longer than len gets WSAEMSGSIZE and the rest is lost");
    send_to_receiver("0123456789ABCDEFGHIJ", 20);
    memset(buf, 'x', sizeof buf);
    T_EQ(CALL(wsock32, recv, r, buf, 8, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEMSGSIZE);
    T_TRUE(memcmp(buf, "01234567", 8) == 0);
    T_TRUE(buf[8] == 'x');
    T_EQ(CALL(wsock32, recv, r, buf, 64, 0), SOCKET_ERROR);     /* the rest is gone */
    T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);

    T_CASE("an empty datagram comes back as 0");
    send_to_receiver(msg, 0);
    T_EQ(CALL(wsock32, recv, r, buf, 16, 0), 0);
    T_EQ(T_LAST_ERROR(), 0);

    T_CASE("len zero or negative, or buf NULL: WSAEFAULT");
    T_EQ(CALL(wsock32, recv, r, buf, 0, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(CALL(wsock32, recv, r, buf, -1, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(CALL(wsock32, recv, r, NULL, 16, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_CASE("shut down for receiving: WSAESHUTDOWN");
    blk = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_loopback(&a, 0);
    a.sin_port = 0;
    T_EQ(rt_net_bind(blk, (SOCKADDR *)&a, sizeof a), 0);
    T_EQ(rt_net_shutdown(blk, SD_RECEIVE), 0);
    T_EQ(CALL(wsock32, recv, blk, buf, 16, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAESHUTDOWN);
    rt_net_close(blk);

    T_CASE("success sets last error to 0");
    send_to_receiver(msg, 4);
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(wsock32, recv, r, buf, 16, 0), 4);
    T_EQ(T_LAST_ERROR(), 0);
    rt_net_close(r);

    /* Traps: flags other than 0 and a blocking receive with nothing queued STOP the core. */
    r = receiver();
    send_to_receiver(msg, 4);
    T_TRAPS(CALL(wsock32, recv, r, buf, 16, MSG_PEEK), "rt_net_recvfrom: not implemented");
    rt_net_close(r);
    blk = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_loopback(&a, 0);
    a.sin_port = 0;
    T_EQ(rt_net_bind(blk, (SOCKADDR *)&a, sizeof a), 0);
    T_TRAPS(CALL(wsock32, recv, blk, buf, 16, 0), "rt_net_recvfrom: not implemented");
}
