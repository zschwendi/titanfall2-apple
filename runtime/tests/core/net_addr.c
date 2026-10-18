/* core/net_addr.c: bind, getsockname, connect, getpeername.  Specification: rt/net.h.
 * Where the expected values come from: the comments of the four functions in rt/net.h, each
 * of which names its source (Microsoft's pages of bind and connect; Wine ws2_32/tests/sock.c
 * test_bind, test_getsockname, test_connect_udp, test_errors, test_getpeername; the reviews'
 * rows NV15 to NV18, D22, D28, D29; "from memory" where the comment says so: T5, T6); the
 * engine's own calls (family 23, length 28, ports 37015 and 37005; family 2, length 16, port
 * 0): the recording, docs/trace/net-calls.md section 1.  Which addresses exist and what a
 * connect meets is THE MACHINE in rt/net.h.  The plan is rt-contract-net.md section 5.
 * No host socket is opened and no packet is sent. */
#include <pthread.h>
#include <stdatomic.h>

#include "net_test.h"

TEST(core, net_addr_bind)
{
    net_start();
    SOCKADDR_IN6 any6 = v6("::", 37015), got6;
    SOCKADDR_IN any4 = v4(0, 0, 0, 0, 0), got4;
    int len;

    T_PLAN(1, "the engine's bind, and the name it gives back") {
        SOCKET s = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        LE_SET();
        T_EQ(rt_net_bind(s, SA(any6), 28), 0);
        LE_ZERO();                                          /* review D22 */
        memset(&got6, 0xAA, sizeof got6);
        len = sizeof got6;
        LE_SET();
        T_EQ(rt_net_getsockname(s, SA(got6), &len), 0);
        LE_ZERO();
        T_EQ(len, 28);
        T_EQ(memcmp(&got6, &any6, sizeof got6), 0);         /* family 23, the port, ::, the other members 0 */

        T_CASE("3: the port is taken for datagram sockets, free for stream sockets");
        SOCKET d2 = rt_net_socket(AF_INET, SOCK_DGRAM, 0), t = rt_net_socket(AF_INET6, SOCK_STREAM, 0);
        SOCKADDR_IN same = v4(0, 0, 0, 0, 37015);
        FAILS(rt_net_bind(d2, SA(same), 16), WSAEADDRINUSE);
        T_EQ(rt_net_bind(t, SA(any6), 28), 0);
        same = v4(0, 0, 0, 0, 37016);                       /* the engine's next try */
        T_EQ(rt_net_bind(d2, SA(same), 16), 0);

        T_CASE("4: the holder closed, the port is free again");
        T_EQ(rt_net_close(s), 0);
        s = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
        T_EQ(rt_net_bind(s, SA(any6), 28), 0);
    }

    T_PLAN(2, "port 0 twice: two ports of the dynamic range, and the address given is kept") {
        SOCKET a = rt_net_socket(AF_INET, SOCK_DGRAM, 0), b = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
        T_EQ(rt_net_bind(a, SA(any4), 16), 0);              /* the chat connection's bind */
        SOCKADDR_IN lo = v4(127, 0, 0, 2, 0);
        T_EQ(rt_net_bind(b, SA(lo), 16), 0);
        USHORT pa = net_port(a), pb = net_port(b);
        T_TRUE(pa >= 49152 && pb >= 49152 && pa != pb);
        len = sizeof got4;
        T_EQ(rt_net_getsockname(b, SA(got4), &len), 0);
        T_EQ(len, 16);
        T_TRUE(got4.sin_family == AF_INET && got4.sin_addr.s_b[0] == 127 && got4.sin_addr.s_b[3] == 2);
    }

    T_PLAN(5, "the machine's addresses are the unspecified and the loopback ones") {
        SOCKET a = rt_net_socket(AF_INET, SOCK_DGRAM, 0), b = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
        SOCKADDR_IN far4 = v4(192, 0, 2, 1, 5000), near4 = v4(127, 0, 0, 2, 5000);
        SOCKADDR_IN6 far6 = v6("2001:db8::1", 5001), near6 = v6("::1", 5001);
        FAILS(rt_net_bind(a, SA(far4), 16), WSAEADDRNOTAVAIL);
        FAILS(rt_net_bind(b, SA(far6), 28), WSAEADDRNOTAVAIL);
        T_EQ(rt_net_bind(a, SA(near4), 16), 0);
        T_EQ(rt_net_bind(b, SA(near6), 28), 0);

        T_CASE("6: a second bind, short and missing names, another family, and their order");
        FAILS(rt_net_bind(a, SA(near4), 16), WSAEINVAL);
        FAILS(rt_net_bind(a, SA(far4), 16), WSAEINVAL);     /* "bound" before the address */
        FAILS(rt_net_bind(a, SA(near6), 28), WSAEAFNOSUPPORT);  /* the family before "bound" */
        FAILS(rt_net_bind(a, SA(near6), 15), WSAEFAULT);    /* the length before the family */
        SOCKET c = rt_net_socket(AF_INET, SOCK_DGRAM, 0), d = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
        FAILS(rt_net_bind(c, SA(near4), 15), WSAEFAULT);
        FAILS(rt_net_bind(d, SA(near6), 27), WSAEFAULT);
        FAILS(rt_net_bind(c, NULL, 16), WSAEFAULT);
        FAILS(rt_net_bind(d, SA(near4), 28), WSAEAFNOSUPPORT);  /* from memory: review T5 */
        SOCKADDR_IN odd = near4;
        odd.sin_family = (SHORT)0xdead;
        FAILS(rt_net_bind(c, SA(odd), 16), WSAEAFNOSUPPORT);
        FAILS(rt_net_bind(c, SA(near4), 16), WSAEADDRINUSE);    /* 5000 is a's */

        T_CASE("7: getsockname before bind, and with a short or missing length");
        len = sizeof got4;
        memset(&got4, 0xAA, sizeof got4);
        FAILS(rt_net_getsockname(c, SA(got4), &len), WSAEINVAL);
        T_EQ(((BYTE *)&got4)[0], 0xAA);
        len = 15;
        FAILS(rt_net_getsockname(a, SA(got4), &len), WSAEFAULT);
        T_EQ(len, 15);
        T_EQ(((BYTE *)&got4)[0], 0xAA);
        FAILS(rt_net_getsockname(a, NULL, &len), WSAEFAULT);
        FAILS(rt_net_getsockname(a, SA(got4), NULL), WSAEFAULT);
        len = 27;
        FAILS(rt_net_getsockname(b, SA(got6), &len), WSAEFAULT);
    }

    T_PLAN(12, "a taken port with SO_REUSEADDR on either socket is a stop") {
        SOCKET a = rt_net_socket(AF_INET, SOCK_DGRAM, 0), b = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
        SOCKET c = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
        int one = 1;
        SOCKADDR_IN n = v4(0, 0, 0, 0, 6000);
        T_EQ(rt_net_setsockopt(a, SOL_SOCKET, SO_REUSEADDR, &one, 4), 0);
        T_EQ(rt_net_bind(a, SA(n), 16), 0);                 /* the port is free: no stop */
        T_TRAPS(rt_net_bind(b, SA(n), 16), "rt_net_bind: not implemented");
        n = v4(0, 0, 0, 0, 6001);
        T_EQ(rt_net_bind(b, SA(n), 16), 0);
        T_EQ(rt_net_setsockopt(c, SOL_SOCKET, SO_REUSEADDR, &one, 4), 0);
        T_TRAPS(rt_net_bind(c, SA(n), 16), "rt_net_bind: not implemented");
    }
}

TEST(core, net_addr_connect)
{
    SOCKADDR_IN to = v4(127, 0, 0, 1, 80), got;
    int len = sizeof got;

    T_PLAN(11, "before any start") {
        FAILS(rt_net_bind(4, SA(to), 16), WSANOTINITIALISED);
        FAILS(rt_net_getsockname(4, SA(got), &len), WSANOTINITIALISED);
        FAILS(rt_net_connect(4, SA(to), 16), WSANOTINITIALISED);
        FAILS(rt_net_getpeername(4, SA(got), &len), WSANOTINITIALISED);
    }
    net_start();
    T_PLAN(11, "not a socket") {
        SOCKET gone = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
        T_EQ(rt_net_close(gone), 0);
        for (SOCKET *p = (SOCKET[]){ gone, 0x100000004ull, 0 }; ; p++) {
            FAILS(rt_net_bind(*p, SA(to), 16), WSAENOTSOCK);
            FAILS(rt_net_getsockname(*p, SA(got), &len), WSAENOTSOCK);
            FAILS(rt_net_connect(*p, SA(to), 16), WSAENOTSOCK);
            FAILS(rt_net_getpeername(*p, SA(got), &len), WSAENOTSOCK);
            if (!*p) break;
        }
    }

    T_PLAN(8, "a stream socket: refused on loopback, no route elsewhere, at once in both modes") {
        SOCKADDR_IN far = v4(192, 0, 2, 1, 443), any = v4(0, 0, 0, 0, 80);
        for (ULONG nonblocking = 0; nonblocking < 2; nonblocking++) {
            SOCKET t = rt_net_socket(AF_INET, SOCK_STREAM, 0);      /* libcurl's socket(2, 1, 0) */
            T_EQ(rt_net_ioctl(t, FIONBIO, &nonblocking), 0);
            FAILS(rt_net_connect(t, SA(any), 16), WSAEADDRNOTAVAIL);    /* review D28 */
            FAILS(rt_net_getsockname(t, SA(got), &len), WSAEINVAL);     /* a refused argument binds nothing */
            FAILS(rt_net_connect(t, SA(to), 16), WSAECONNREFUSED);
            T_TRUE(net_port(t) >= 49152);                   /* an unbound socket is bound first */
            FAILS(rt_net_connect(t, SA(far), 16), WSAENETUNREACH);
            FAILS(rt_net_getpeername(t, SA(got), &len), WSAENOTCONN);
        }
        T_TRUE(T_LOGGED(RT_CH_NET, "connect 192.0.2.1: no route"));
        T_TRUE(T_LOGGED(RT_CH_NET, "connect 127.0.0.1 port 80: refused"));
        SOCKET t6 = rt_net_socket(AF_INET6, SOCK_STREAM, 0);
        SOCKADDR_IN6 lo6 = v6("::1", 80), far6 = v6("2001:db8::1", 80), any6 = v6("::", 80);
        FAILS(rt_net_connect(t6, SA(lo6), 28), WSAECONNREFUSED);
        FAILS(rt_net_connect(t6, SA(far6), 28), WSAENETUNREACH);
        FAILS(rt_net_connect(t6, SA(any6), 28), WSAEADDRNOTAVAIL);
        FAILS(rt_net_connect(t6, SA(lo6), 27), WSAEFAULT);
        FAILS(rt_net_connect(t6, NULL, 28), WSAEFAULT);
        FAILS(rt_net_connect(t6, SA(to), 28), WSAEAFNOSUPPORT);
        SOCKADDR unspec = { 0 };
        T_TRAPS(rt_net_connect(t6, &unspec, 16), "rt_net_connect: not implemented");
        SOCKADDR_IN port0 = v4(127, 0, 0, 1, 0);
        SOCKET t4 = rt_net_socket(AF_INET, SOCK_STREAM, 0);
        T_TRAPS(rt_net_connect(t4, SA(port0), 16), "rt_net_connect: not implemented");
    }

    T_PLAN(10, "getpeername with no peer: WSAENOTCONN before the pointers are looked at") {
        SOCKET d = rt_net_socket(AF_INET, SOCK_DGRAM, 0), t = rt_net_socket(AF_INET, SOCK_STREAM, 0);
        FAILS(rt_net_getpeername(d, SA(got), &len), WSAENOTCONN);
        FAILS(rt_net_getpeername(t, SA(got), &len), WSAENOTCONN);
        FAILS(rt_net_getpeername(d, NULL, NULL), WSAENOTCONN);          /* review D29 */
        FAILS(rt_net_getpeername(t, NULL, NULL), WSAENOTCONN);
    }

    T_PLAN(9, "a datagram socket: a loopback peer is stored, and taken away again") {
        SOCKET d = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
        SOCKADDR_IN peer = v4(127, 0, 0, 1, 4000), far = v4(192, 0, 2, 1, 4000);
        LE_SET();
        T_EQ(rt_net_connect(d, SA(peer), 16), 0);
        LE_UNTOUCHED();                                     /* this runtime's rule: rt/net.h, LAST ERROR */
        T_TRUE(net_port(d) >= 49152);
        memset(&got, 0xAA, sizeof got);
        len = sizeof got;
        LE_SET();
        T_EQ(rt_net_getpeername(d, SA(got), &len), 0);
        LE_UNTOUCHED();
        T_EQ(len, 16);
        T_EQ(memcmp(&got, &peer, sizeof got), 0);
        len = 15;
        FAILS(rt_net_getpeername(d, SA(got), &len), WSAEFAULT);
        T_EQ(len, 15);
        FAILS(rt_net_getpeername(d, NULL, &len), WSAEFAULT);
        FAILS(rt_net_getpeername(d, SA(got), NULL), WSAEFAULT);
        FAILS(rt_net_connect(d, SA(far), 16), WSAENETUNREACH);
        peer = v4(127, 0, 0, 1, 4001);                      /* it may be repeated with another peer */
        T_EQ(rt_net_connect(d, SA(peer), 16), 0);
        len = sizeof got;
        T_EQ(rt_net_getpeername(d, SA(got), &len), 0);
        T_EQ(port_of(&got), 4001);
        SOCKADDR unspec = { 0 };
        T_EQ(rt_net_connect(d, &unspec, 16), 0);            /* review D28 */
        FAILS(rt_net_getpeername(d, SA(got), &len), WSAENOTCONN);
        T_TRAPS(rt_net_connect(d, &unspec, 15), "rt_net_connect: not implemented");
        SOCKADDR_IN any = v4(0, 0, 0, 0, 4000), port0 = v4(127, 0, 0, 1, 0);
        T_TRAPS(rt_net_connect(d, SA(any), 16), "rt_net_connect: not implemented");
        T_TRAPS(rt_net_connect(d, SA(port0), 16), "rt_net_connect: not implemented");

        T_CASE("9: on an AF_INET6 datagram socket 16 zero bytes are enough to take the peer away");
        SOCKET d6 = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
        SOCKADDR_IN6 peer6 = v6("::1", 4000), got6;
        T_EQ(rt_net_connect(d6, SA(peer6), 28), 0);
        len = sizeof got6;
        T_EQ(rt_net_getpeername(d6, SA(got6), &len), 0);
        T_EQ(len, 28);
        T_EQ(memcmp(&got6, &peer6, sizeof got6), 0);
        T_EQ(rt_net_connect(d6, &unspec, 16), 0);
        FAILS(rt_net_getpeername(d6, SA(got6), &len), WSAENOTCONN);
        SOCKADDR_IN6 mapped = v6("::ffff:127.0.0.1", 4000);
        T_TRAPS(rt_net_connect(d6, SA(mapped), 28), "rt_net_connect: not implemented");     /* IPV6_V6ONLY is 1 */
        int zero = 0;
        SOCKET dual = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
        T_EQ(rt_net_setsockopt(dual, IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4), 0);
        T_EQ(rt_net_connect(dual, SA(mapped), 28), 0);
    }
}

static atomic_uint bind_bad;
static atomic_uchar port_seen[65536];

static void *binder(void *arg) {
    (void)arg;
    for (int i = 0; i < 100; i++) {
        SOCKET s = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
        SOCKADDR_IN any = { .sin_family = AF_INET }, got;
        int len = sizeof got;
        if (rt_net_bind(s, SA(any), 16) != 0 || rt_net_getsockname(s, SA(got), &len) != 0) { atomic_fetch_add(&bind_bad, 1); continue; }
        USHORT port = (USHORT)((got.sin_port >> 8) | (got.sin_port << 8));
        if (port < 49152 || atomic_exchange(&port_seen[port], 1)) atomic_fetch_add(&bind_bad, 1);
    }
    return NULL;
}

TEST(core, net_addr_threads)
{
    T_PLAN(13, "8 threads bind port 0 on 100 sockets each: 800 different ports") {
        net_start();
        pthread_t t[8];
        for (int i = 0; i < 8; i++) T_EQ(pthread_create(&t[i], NULL, binder, NULL), 0);
        for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
        T_EQ(atomic_load(&bind_bad), 0);
        unsigned n = 0;
        for (unsigned p = 0; p < 65536; p++) n += atomic_load(&port_seen[p]);
        T_EQ(n, 800);
        T_EQ(rt_net_test_open_count(), 800);
    }
}
