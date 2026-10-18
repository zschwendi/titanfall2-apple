#include "rt/test.h"
#include "rt/net.h"

/* Ports are given in network byte order: 4000 is 0xA00F, 0 is 0. */
TEST(wsock32, getsockname)
{
    WSADATA d;
    SOCKET s1, s2, s3, s4;
    SOCKADDR_IN a4 = { .sin_family = AF_INET, .sin_port = 0xA00F };
    SOCKADDR_IN g4;
    SOCKADDR_IN6 a6 = { .sin6_family = AF_INET6 };          /* :: */
    SOCKADDR_IN6 g6 = { 0 };
    int gl;
    unsigned zero;

    T_CASE("bound AF_INET socket gives its family, port, address and sin_zero with namelen 16");
    T_EQ(rt_net_startup(0x0202, &d), 0);
    s1 = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s1 != INVALID_SOCKET);
    a4.sin_addr.s_b[0] = 127;
    a4.sin_addr.s_b[1] = 0;
    a4.sin_addr.s_b[2] = 0;
    a4.sin_addr.s_b[3] = 1;
    T_EQ(rt_net_bind(s1, (const SOCKADDR *)&a4, 16), 0);
    memset(&g4, 0xAA, sizeof g4);
    gl = 16;
    rt_set_last_error(T_CTX(), 0xdead);
    T_EQ(CALL(wsock32, getsockname, s1, (SOCKADDR *)&g4, &gl), 0);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(gl, 16);
    T_EQ(g4.sin_family, AF_INET);
    T_EQ(g4.sin_port, 0xA00F);
    T_EQ(g4.sin_addr.s_b[0], 127);
    T_EQ(g4.sin_addr.s_b[1], 0);
    T_EQ(g4.sin_addr.s_b[2], 0);
    T_EQ(g4.sin_addr.s_b[3], 1);
    zero = 0;
    for (int i = 0; i < 8; i++) zero |= (unsigned char)g4.sin_zero[i];
    T_EQ(zero, 0);

    T_CASE("bound AF_INET6 socket on :: and port 0 gives namelen 28 and a port that is not 0");
    s2 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s2 != INVALID_SOCKET);
    a6.sin6_port = 0;
    T_EQ(rt_net_bind(s2, (const SOCKADDR *)&a6, 28), 0);
    memset(&g6, 0xAA, sizeof g6);
    gl = 28;
    T_EQ(CALL(wsock32, getsockname, s2, (SOCKADDR *)&g6, &gl), 0);
    T_EQ(gl, 28);
    T_EQ(g6.sin6_family, AF_INET6);
    T_TRUE(g6.sin6_port != 0);

    T_CASE("unbound socket gets WSAEINVAL");
    s3 = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s3 != INVALID_SOCKET);
    gl = 16;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getsockname, s3, (SOCKADDR *)&g4, &gl), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);

    T_CASE("namelen 15 gets WSAEFAULT");
    gl = 15;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getsockname, s1, (SOCKADDR *)&g4, &gl), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(gl, 15);

    T_CASE("not a socket gets WSAENOTSOCK");
    s4 = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    T_REQUIRE(s4 != INVALID_SOCKET);
    T_EQ(rt_net_close(s4), 0);
    gl = 16;
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(wsock32, getsockname, s4, (SOCKADDR *)&g4, &gl), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);

    T_CASE("success sets last error to 0");
    gl = 16;
    rt_set_last_error(T_CTX(), 0xdead);
    T_EQ(CALL(wsock32, getsockname, s1, (SOCKADDR *)&g4, &gl), 0);
    T_EQ(T_LAST_ERROR(), 0);

    T_EQ(rt_net_close(s1), 0);
    T_EQ(rt_net_close(s2), 0);
    T_EQ(rt_net_close(s3), 0);
}
