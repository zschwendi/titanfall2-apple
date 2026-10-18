#include "rt/test.h"
#include "rt/net.h"

/* Ports are given in network byte order: 4000 is 0xA00F. */
TEST(wsock32, getpeername)
{
    WSADATA d;
    SOCKET dg, st, s3;
    SOCKADDR_IN peer = { .sin_family = AF_INET, .sin_port = 0xA00F };   /* 127.0.0.1:4000 */
    SOCKADDR_IN g = { 0 };
    SOCKADDR_IN before;
    int len;

    peer.sin_addr.s_b[0] = 127;
    peer.sin_addr.s_b[3] = 1;

    T_EQ(rt_net_startup(0x0202, &d), 0);
    dg = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(dg != INVALID_SOCKET);
    st = rt_net_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    T_REQUIRE(st != INVALID_SOCKET);

    T_CASE("a new datagram or stream socket gets WSAENOTCONN, also with NULL name and NULL namelen");
    len = 16;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getpeername, dg, (SOCKADDR *)&g, &len), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);
    T_EQ(len, 16);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getpeername, dg, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getpeername, st, NULL, NULL), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getpeername, st, (SOCKADDR *)&g, &len), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTCONN);

    T_CASE("after connect to 127.0.0.1 port 4000 the datagram socket reports that address and port with *namelen 16");
    T_EQ(rt_net_connect(dg, (const SOCKADDR *)&peer, 16), 0);
    memset(&g, 0xAA, sizeof g);                 /* every byte must be written by the call */
    len = 16;
    rt_set_last_error(T_CTX(), 0xDEAD);
    T_EQ(CALL(wsock32, getpeername, dg, (SOCKADDR *)&g, &len), 0);
    T_EQ(T_LAST_ERROR(), 0xDEAD);               /* success leaves last error alone */
    T_EQ(len, 16);
    T_EQ(g.sin_family, AF_INET);
    T_EQ(g.sin_port, 0xA00F);
    T_EQ(g.sin_addr.s_b[0], 127);
    T_EQ(g.sin_addr.s_b[1], 0);
    T_EQ(g.sin_addr.s_b[2], 0);
    T_EQ(g.sin_addr.s_b[3], 1);
    for (int i = 0; i < 8; i++) T_EQ(g.sin_zero[i], 0);

    T_CASE("*namelen 15 gets WSAEFAULT and stays 15");
    memset(&before, 0x55, sizeof before);
    memcpy(&g, &before, sizeof g);
    len = 15;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getpeername, dg, (SOCKADDR *)&g, &len), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(len, 15);
    T_TRUE(memcmp(&g, &before, sizeof g) == 0);  /* name untouched */

    T_CASE("not a socket gets WSAENOTSOCK");
    s3 = (SOCKET)0x7FFFFFF0;                     /* never handed out */
    len = 16;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getpeername, s3, (SOCKADDR *)&g, &len), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(len, 16);

    rt_net_close(dg);
    rt_net_close(st);
}
