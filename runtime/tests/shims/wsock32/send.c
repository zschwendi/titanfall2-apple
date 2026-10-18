#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/win/net.h"

/* Port 4000 in network byte order, written by hand. */
#define PORT_4000  ((USHORT)0xA00F)

static char big[65508];

static void addr_ipv4(SOCKADDR_IN *a, int b0, int b1, int b2, int b3, USHORT port)
{
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET;
    a->sin_port = port;
    a->sin_addr.s_b[0] = (BYTE)b0;
    a->sin_addr.s_b[1] = (BYTE)b1;
    a->sin_addr.s_b[2] = (BYTE)b2;
    a->sin_addr.s_b[3] = (BYTE)b3;
}

/* A bound, non-blocking AF_INET datagram socket on 127.0.0.1:4000. */
static SOCKET receiver(void)
{
    SOCKADDR_IN a;
    ULONG one = 1;
    SOCKET s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_ipv4(&a, 127, 0, 0, 1, PORT_4000);
    T_EQ(rt_net_bind(s, (SOCKADDR *)&a, sizeof a), 0);
    T_EQ(rt_net_ioctl(s, FIONBIO, &one), 0);
    return s;
}

TEST(wsock32, send)
{
    WSADATA d;
    SOCKET s, r, c;
    SOCKADDR_IN to, from, any;
    char buf[64];
    char out[64];
    int fromlen;
    T_EQ(rt_net_startup(0x0202, &d), 0);

    addr_ipv4(&to, 127, 0, 0, 1, PORT_4000);
    memcpy(buf, "abc", 3);

    T_CASE("not a socket gets WSAENOTSOCK");
    T_EQ(CALL(wsock32, send, (SOCKET)12345, buf, 3, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("stream socket gets WSAENOTCONN");
    s = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_EQ(CALL(wsock32, send, s, buf, 3, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);
    rt_net_close(s);

    T_CASE("datagram socket with no peer gets WSAENOTCONN");
    s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_EQ(CALL(wsock32, send, s, buf, 3, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);

    T_CASE("len below zero, or buf NULL with len above zero: WSAEFAULT");
    T_EQ(CALL(wsock32, send, s, buf, -1, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(CALL(wsock32, send, s, NULL, 3, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    rt_net_close(s);

    T_CASE("shut down for sending: WSAESHUTDOWN");
    s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_EQ(rt_net_connect(s, (SOCKADDR *)&to, sizeof to), 0);
    T_EQ(rt_net_shutdown(s, SD_SEND), 0);
    T_EQ(CALL(wsock32, send, s, buf, 3, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAESHUTDOWN);
    rt_net_close(s);

    T_CASE("loopback datagram is queued on the receiver and its length comes back");
    r = receiver();
    c = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_EQ(rt_net_connect(c, (SOCKADDR *)&to, sizeof to), 0);
    T_EQ(CALL(wsock32, send, c, buf, 3, 0), 3);
    fromlen = sizeof from;
    memset(out, 'x', sizeof out);
    T_EQ(rt_net_recvfrom(r, out, sizeof out, 0, (SOCKADDR *)&from, &fromlen), 3);
    T_TRUE(memcmp(out, "abc", 3) == 0);
    T_TRUE(out[3] == 'x');
    rt_net_close(c);

    T_CASE("connected datagram socket sends to its peer");
    c = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_EQ(rt_net_connect(c, (SOCKADDR *)&to, sizeof to), 0);
    T_EQ(CALL(wsock32, send, c, "xyz", 3, 0), 3);
    fromlen = sizeof from;
    T_EQ(rt_net_recvfrom(r, out, sizeof out, 0, (SOCKADDR *)&from, &fromlen), 3);
    T_TRUE(memcmp(out, "xyz", 3) == 0);
    T_EQ(T_LAST_ERROR(), 0);
    rt_net_close(c);
    rt_net_close(r);

    T_CASE("len above 65507: WSAEMSGSIZE");
    s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_ipv4(&any, 127, 0, 0, 1, PORT_4000);
    T_EQ(rt_net_connect(s, (SOCKADDR *)&any, sizeof any), 0);
    T_EQ(CALL(wsock32, send, s, big, 65508, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEMSGSIZE);
    rt_net_close(s);

    T_CASE("success sets last error to 0");
    s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_ipv4(&any, 127, 0, 0, 1, PORT_4000);
    T_EQ(rt_net_connect(s, (SOCKADDR *)&any, sizeof any), 0);
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(wsock32, send, s, buf, 3, 0), 3);
    T_EQ(T_LAST_ERROR(), 0);
    rt_net_close(s);

    /* Trap: flags other than 0 STOP the core. */
    s = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    addr_ipv4(&any, 127, 0, 0, 1, PORT_4000);
    T_EQ(rt_net_connect(s, (SOCKADDR *)&any, sizeof any), 0);
    T_TRAPS(CALL(wsock32, send, s, buf, 3, MSG_OOB), "rt_net_sendto: not implemented");
    rt_net_close(s);
}
