#include "rt/test.h"
#include "rt/net.h"

/* Addresses are filled by hand; ports are in network byte order (4000 is 0xA00F). */
static void addr4(SOCKADDR_IN *a, BYTE b0, BYTE b1, BYTE b2, BYTE b3, USHORT port)
{
    a->sin_family = AF_INET;
    a->sin_port = port;
    a->sin_addr.s_addr = 0;
    a->sin_addr.s_b[0] = b0;
    a->sin_addr.s_b[1] = b1;
    a->sin_addr.s_b[2] = b2;
    a->sin_addr.s_b[3] = b3;
}

static char big[65508];

TEST(wsock32, sendto)
{
    WSADATA d;
    SOCKET rx, tx, tx2, st, u6, c;
    SOCKADDR_IN to, to0, unspec, nonloop, from;
    SOCKADDR_IN6 to6;
    const char msg[5] = { 'h', 'e', 'l', 'l', 'o' };
    char got[16];
    int fl = sizeof from, one = 1, i, ok;

    T_EQ(rt_net_startup(0x0202, &d), 0);

    rx = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    addr4(&to, 127, 0, 0, 1, 0xA00F);            /* 127.0.0.1:4000, the bound receiver */
    T_EQ(rt_net_bind(rx, (const SOCKADDR *)&to, 16), 0);
    tx = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    addr4(&to0, 127, 0, 0, 1, 0);                /* 127.0.0.1 port 0 */
    addr4(&unspec, 0, 0, 0, 0, 0xA00F);          /* 0.0.0.0:4000 */
    addr4(&nonloop, 192, 0, 2, 1, 0xA00F);       /* 192.0.2.1:4000, documentation range */
    to6.sin6_family = AF_INET6;
    to6.sin6_port = 0xA00F;
    for (i = 0; i < 16; i++) to6.sin6_addr.s6_addr[i] = 0;
    to6.sin6_addr.s6_addr[10] = 0xFF;            /* ::ffff:127.0.0.1 */
    to6.sin6_addr.s6_addr[11] = 0xFF;
    to6.sin6_addr.s6_addr[12] = 127;
    to6.sin6_addr.s6_addr[15] = 1;
    u6 = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
    T_EQ(rt_net_setsockopt(u6, IPPROTO_IPV6, IPV6_V6ONLY, &one, (int)sizeof one), 0);

    T_CASE("5 bytes to a bound loopback socket: 5, and the receiver gets them");
    T_EQ(CALL(wsock32, sendto, tx, msg, 5, 0, (const SOCKADDR *)&to, 16), 5);
    T_EQ(rt_net_recvfrom(rx, got, sizeof got, 0, (SOCKADDR *)&from, &fl), 5);
    ok = 1;
    for (i = 0; i < 5; i++) if (got[i] != msg[i]) ok = 0;
    T_TRUE(ok);
    T_EQ(fl, 16);

    T_CASE("5 bytes to a loopback port nobody holds: 5");
    addr4(&to, 127, 0, 0, 1, 0xA10F);            /* 127.0.0.1:4001, held by nobody */
    T_EQ(CALL(wsock32, sendto, tx, msg, 5, 0, (const SOCKADDR *)&to, 16), 5);

    T_CASE("to a non-loopback address: WSAENETUNREACH and a log line");
    T_EQ(CALL(wsock32, sendto, tx, msg, 5, 0, (const SOCKADDR *)&nonloop, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENETUNREACH);
    T_LOGGED(RT_CH_NET, "sendto 192.0.2.1");

    T_CASE("to the unspecified address: WSAEADDRNOTAVAIL");
    T_EQ(CALL(wsock32, sendto, tx, msg, 5, 0, (const SOCKADDR *)&unspec, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEADDRNOTAVAIL);

    T_CASE("16 bytes from an unbound socket to port 0 on loopback: 16");
    tx2 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_EQ(CALL(wsock32, sendto, tx2, got, 16, 0, (const SOCKADDR *)&to0, 16), 16);

    T_CASE("a send that succeeds leaves last error 0");
    CALL(kernel32, SetLastError, 0xDEAD);
    addr4(&to, 127, 0, 0, 1, 0xA10F);
    T_EQ(CALL(wsock32, sendto, tx, msg, 5, 0, (const SOCKADDR *)&to, 16), 5);
    T_EQ(T_LAST_ERROR(), 0);

    T_CASE("a datagram longer than 65507 bytes: WSAEMSGSIZE");
    T_EQ(CALL(wsock32, sendto, tx, big, 65508, 0, (const SOCKADDR *)&to, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEMSGSIZE);

    T_CASE("a stream socket that is not connected: WSAENOTCONN");
    st = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_EQ(CALL(wsock32, sendto, st, msg, 5, 0, (const SOCKADDR *)&to, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);

    T_CASE("not a socket: WSAENOTSOCK");
    c = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_EQ(rt_net_close(c), 0);
    T_EQ(CALL(wsock32, sendto, c, msg, 5, 0, (const SOCKADDR *)&to, 16), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    /* The core's stops: each one ends the run, so each is proved in its own fork. */
    T_TRAPS(CALL(wsock32, sendto, tx, msg, 5, 1, (const SOCKADDR *)&to, 16),
            "rt_net_sendto: not implemented");

    T_TRAPS(CALL(wsock32, sendto, tx, msg, 5, 0, (const SOCKADDR *)&to6, 28),
            "rt_net_sendto: not implemented");

    tx2 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    addr4(&to, 127, 0, 0, 1, 0xA10F);            /* connect to 127.0.0.1:4001; port 0 STOPS in connect */
    T_EQ(rt_net_connect(tx2, (const SOCKADDR *)&to, 16), 0);
    T_TRAPS(CALL(wsock32, sendto, tx2, msg, 5, 0, (const SOCKADDR *)&to, 16),
            "rt_net_sendto: not implemented");

    T_TRAPS(CALL(wsock32, sendto, u6, msg, 5, 0, (const SOCKADDR *)&to6, 28),
            "rt_net_sendto: not implemented");
}
