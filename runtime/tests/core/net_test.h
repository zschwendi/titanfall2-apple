/* Shared by the tests of the socket layer's pieces (tests/core/net_*.c): names and sockets. */
#ifndef RT_TESTS_CORE_NET_TEST_H
#define RT_TESTS_CORE_NET_TEST_H

#include <string.h>

#include "rt/test.h"
#include "rt/log.h"
#include "rt/net.h"
#include "rt/thread.h"

unsigned rt_net_test_open_count(void);
unsigned rt_net_test_start_count(void);

#define LE_SET()        rt_thread_set_last_error(0xDEAD)
#define LE_UNTOUCHED()  T_EQ(T_LAST_ERROR(), 0xDEAD)
#define LE_ZERO()       T_EQ(T_LAST_ERROR(), 0)
/* A call that fails: SOCKET_ERROR and the code. */
#define FAILS(call, code)  do { LE_SET(); T_EQ((call), SOCKET_ERROR); T_EQ(T_LAST_ERROR(), (code)); } while (0)
#define SA(x)  ((SOCKADDR *)&(x))

static inline void net_start(void) {
    WSADATA d;
    T_EQ(rt_net_startup(0x0202, &d), 0);
}

static inline SOCKADDR_IN v4(BYTE a, BYTE b, BYTE c, BYTE d, USHORT port) {
    SOCKADDR_IN n = { .sin_family = AF_INET, .sin_port = (USHORT)((port >> 8) | (port << 8)) };
    n.sin_addr.s_b[0] = a; n.sin_addr.s_b[1] = b; n.sin_addr.s_b[2] = c; n.sin_addr.s_b[3] = d;
    return n;
}

static inline SOCKADDR_IN6 v6(const char *text, USHORT port) {
    SOCKADDR_IN6 n = { .sin6_family = AF_INET6, .sin6_port = (USHORT)((port >> 8) | (port << 8)) };
    T_EQ(rt_net_pton(AF_INET6, text, &n.sin6_addr), 1);
    return n;
}

static inline USHORT port_of(const void *name) {       /* sin_port and sin6_port are at the same offset */
    USHORT p = ((const SOCKADDR_IN *)name)->sin_port;
    return (USHORT)((p >> 8) | (p << 8));
}

/* A datagram socket bound to the unspecified address and `port` (0: any).  v6only: 0 or 1 for
 * an AF_INET6 socket, -1 to leave the option alone. */
static inline SOCKET net_udp(int af, int v6only, USHORT port, bool nonblocking) {
    SOCKET s = rt_net_socket(af, SOCK_DGRAM, IPPROTO_UDP);
    T_TRUE(s != INVALID_SOCKET);
    if (v6only >= 0) T_EQ(rt_net_setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, 4), 0);
    ULONG on = nonblocking;
    T_EQ(rt_net_ioctl(s, FIONBIO, &on), 0);
    if (af == AF_INET) { SOCKADDR_IN n = v4(0, 0, 0, 0, port); T_EQ(rt_net_bind(s, SA(n), sizeof n), 0); }
    else { SOCKADDR_IN6 n = v6("::", port); T_EQ(rt_net_bind(s, SA(n), sizeof n), 0); }
    return s;
}

static inline USHORT net_port(SOCKET s) {
    SOCKADDR_IN6 n;
    int len = sizeof n;
    T_EQ(rt_net_getsockname(s, SA(n), &len), 0);
    return port_of(&n);
}

#endif
