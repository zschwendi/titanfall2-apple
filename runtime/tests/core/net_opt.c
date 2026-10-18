/* core/net_opt.c: setsockopt, getsockopt, ioctlsocket.  Specification: rt/net.h.
 * Where the expected values come from: the comments of the three functions in rt/net.h, each
 * of which names its source (Wine ws2_32/tests/sock.c test_set_getsockopt with its
 * test_optsize table, test_ipv6only, test_extendedSocketOptions, test_fionbio; the reviews'
 * rows NV19, NV20, C11, D22, D30, D31, D32; "from memory" where the comment says so: T2, T7);
 * the options the real game set and read, with their lengths: the recording,
 * docs/trace/net-calls.md sections 1, 2 and 5.  The plan is rt-contract-net.md section 5.
 * No host socket is opened and no packet is sent. */
#include "net_test.h"

/* A four-byte option reads as `want`: *optlen 4, the byte after the value untouched, last error 0. */
static void reads4(SOCKET s, int level, int opt, int want) {
    BYTE buf[8];
    memset(buf, 0xAA, sizeof buf);
    int len = 8, v;
    LE_SET();
    T_EQ(rt_net_getsockopt(s, level, opt, buf, &len), 0);
    LE_ZERO();                                              /* review D22 */
    memcpy(&v, buf, 4);
    T_EQ(len, 4);
    T_EQ(v, want);
    T_EQ(buf[4], 0xAA);
}

/* A one-byte option reads as `want`: only the low byte of an int changes, *optlen 1 (D30). */
static void reads1(SOCKET s, int level, int opt, int want) {
    unsigned v = 0xdeadbeef;
    int len = 4;
    LE_SET();
    T_EQ(rt_net_getsockopt(s, level, opt, &v, &len), 0);
    LE_ZERO();
    T_EQ(len, 1);
    T_EQ(v, 0xdeadbe00u | (unsigned)want);
}

static void sets(SOCKET s, int level, int opt, int value, int optlen) {
    LE_SET();
    T_EQ(rt_net_setsockopt(s, level, opt, &value, optlen), 0);
    LE_ZERO();
}

TEST(core, net_opt_values)
{
    net_start();
    SOCKET d6 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP), d4 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    SOCKET t = rt_net_socket(AF_INET, SOCK_STREAM, 0);

    T_PLAN(1, "the values of a new socket") {
        for (SOCKET *p = (SOCKET[]){ d6, d4, t, 0 }; *p; p++) {
            reads4(*p, SOL_SOCKET, SO_REUSEADDR, 0);
            reads4(*p, SOL_SOCKET, SO_BROADCAST, 0);
            reads1(*p, SOL_SOCKET, SO_KEEPALIVE, 0);
            reads4(*p, SOL_SOCKET, SO_SNDBUF, 65536);       /* from memory: review T2 */
            reads4(*p, SOL_SOCKET, SO_RCVBUF, 65536);
            reads4(*p, SOL_SOCKET, SO_ERROR, 0);
        }
        reads1(d6, IPPROTO_IPV6, IPV6_V6ONLY, 1);
        reads1(t, IPPROTO_TCP, TCP_NODELAY, 0);
        LINGER lg = { 7, 7 };
        int len = sizeof lg;
        T_EQ(rt_net_getsockopt(t, SOL_SOCKET, SO_LINGER, &lg, &len), 0);
        T_TRUE(len == 4 && lg.l_onoff == 0 && lg.l_linger == 0);
    }

    T_PLAN(2, "the calls the real game made, read back") {
        sets(d6, SOL_SOCKET, SO_SNDBUF, 0x20000, 4);        /* engine+0x21ad93 */
        sets(d6, SOL_SOCKET, SO_RCVBUF, 0x20000, 4);        /* engine+0x21adc3 */
        sets(d6, IPPROTO_IPV6, IPV6_V6ONLY, 0, 4);          /* engine+0x21adf2 */
        sets(d6, SOL_SOCKET, SO_REUSEADDR, 1, 4);           /* with -reuse on the command line */
        reads4(d6, SOL_SOCKET, SO_SNDBUF, 0x20000);
        reads4(d6, SOL_SOCKET, SO_RCVBUF, 0x20000);
        reads1(d6, IPPROTO_IPV6, IPV6_V6ONLY, 0);
        reads4(d6, SOL_SOCKET, SO_REUSEADDR, 1);
        sets(t, IPPROTO_TCP, TCP_NODELAY, 1, 4);            /* libcurl, engine+0x26137a */
        reads1(t, IPPROTO_TCP, TCP_NODELAY, 1);
        reads4(t, SOL_SOCKET, SO_ERROR, 0);                 /* libcurl after a connect, engine+0x2620d0 */
        sets(t, SOL_SOCKET, SO_KEEPALIVE, 1, 4);
        reads1(t, SOL_SOCKET, SO_KEEPALIVE, 1);
        LINGER lg = { 1, 30 }, back = { 0, 0 };
        int len = sizeof back;
        T_EQ(rt_net_setsockopt(t, SOL_SOCKET, SO_LINGER, &lg, sizeof lg), 0);
        T_EQ(rt_net_getsockopt(t, SOL_SOCKET, SO_LINGER, &back, &len), 0);
        T_TRUE(back.l_onoff == 1 && back.l_linger == 30);
    }

    T_PLAN(3, "a BOOL is 0 or 1; a one-byte option reads only its first byte; a short BOOL is taken") {
        sets(d4, SOL_SOCKET, SO_BROADCAST, 5, 4);
        reads4(d4, SOL_SOCKET, SO_BROADCAST, 1);
        sets(t, IPPROTO_TCP, TCP_NODELAY, 0x100, 4);        /* review D30 */
        reads1(t, IPPROTO_TCP, TCP_NODELAY, 0);
        sets(d4, SOL_SOCKET, SO_REUSEADDR, 1, 1);
        reads4(d4, SOL_SOCKET, SO_REUSEADDR, 1);
        sets(d4, SOL_SOCKET, SO_KEEPALIVE, 1, 1);
        reads1(d4, SOL_SOCKET, SO_KEEPALIVE, 1);
    }

    T_PLAN(4, "options that do not go with the socket") {
        int v = 1, len = 4;
        FAILS(rt_net_setsockopt(d4, IPPROTO_TCP, TCP_NODELAY, &v, 4), WSAENOPROTOOPT);      /* from memory: review T7 */
        FAILS(rt_net_getsockopt(d4, IPPROTO_TCP, TCP_NODELAY, &v, &len), WSAENOPROTOOPT);
        T_TRAPS(rt_net_getsockopt(d4, IPPROTO_IPV6, IPV6_V6ONLY, &v, &len), "rt_net_getsockopt: not implemented");
        sets(d4, IPPROTO_IPV6, IPV6_V6ONLY, 0, 4);          /* review D31 */
        reads1(d4, IPPROTO_IPV6, IPV6_V6ONLY, 0);
        LINGER lg = { 0, 0 };
        len = sizeof lg;
        FAILS(rt_net_getsockopt(d4, SOL_SOCKET, SO_LINGER, &lg, &len), WSAENOPROTOOPT);    /* review D32 */
        T_TRAPS(rt_net_setsockopt(d4, SOL_SOCKET, SO_LINGER, &lg, sizeof lg), "rt_net_setsockopt: not implemented");
    }

    T_PLAN(6, "the read-only options") {
        reads4(d6, SOL_SOCKET, SO_TYPE, SOCK_DGRAM);
        reads4(t, SOL_SOCKET, SO_TYPE, SOCK_STREAM);
        int v = 1, len = 4;
        FAILS(rt_net_setsockopt(t, SOL_SOCKET, SO_TYPE, &v, 4), WSAENOPROTOOPT);    /* review NV19 */
        T_TRAPS(rt_net_setsockopt(t, SOL_SOCKET, SO_ERROR, &v, 4), "rt_net_setsockopt: not implemented");
        rt_thread_set_last_error(WSAEWOULDBLOCK);           /* review C11 */
        v = 77;
        T_EQ(rt_net_getsockopt(t, SOL_SOCKET, SO_ERROR, &v, &len), 0);
        T_EQ(v, 0);
        T_EQ(T_LAST_ERROR(), 0);
    }

    T_PLAN(10, "IPV6_V6ONLY after bind is a stop") {
        SOCKET b = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
        SOCKADDR_IN6 any = v6("::", 0);
        int zero = 0;
        T_EQ(rt_net_bind(b, SA(any), sizeof any), 0);
        T_TRAPS(rt_net_setsockopt(b, IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4), "rt_net_setsockopt: not implemented");
        reads1(b, IPPROTO_IPV6, IPV6_V6ONLY, 1);            /* reading it stays possible */
    }
}

TEST(core, net_opt_errors)
{
    int v = 1, len = 4;
    ULONG arg = 1;

    T_PLAN(9, "before any start") {
        FAILS(rt_net_setsockopt(4, SOL_SOCKET, SO_RCVBUF, &v, 4), WSANOTINITIALISED);
        FAILS(rt_net_getsockopt(4, SOL_SOCKET, SO_RCVBUF, &v, &len), WSANOTINITIALISED);
        FAILS(rt_net_ioctl(4, FIONBIO, &arg), WSANOTINITIALISED);
    }
    net_start();
    SOCKET s = rt_net_socket(AF_INET, SOCK_DGRAM, 0), t = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_PLAN(9, "not a socket") {
        SOCKET gone = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
        T_EQ(rt_net_close(gone), 0);
        for (SOCKET *p = (SOCKET[]){ gone, 0x100000004ull, 0 }; ; p++) {
            FAILS(rt_net_setsockopt(*p, SOL_SOCKET, SO_RCVBUF, &v, 4), WSAENOTSOCK);
            FAILS(rt_net_getsockopt(*p, SOL_SOCKET, SO_RCVBUF, &v, &len), WSAENOTSOCK);
            FAILS(rt_net_ioctl(*p, FIONBIO, &arg), WSAENOTSOCK);
            if (!*p) break;
        }
    }

    T_PLAN(5, "missing pointers and short lengths") {
        FAILS(rt_net_setsockopt(s, SOL_SOCKET, SO_RCVBUF, NULL, 4), WSAEFAULT);
        FAILS(rt_net_setsockopt(s, SOL_SOCKET, SO_SNDBUF, &v, 3), WSAEFAULT);
        FAILS(rt_net_setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &v, 0), WSAEFAULT);
        FAILS(rt_net_setsockopt(t, SOL_SOCKET, SO_LINGER, &v, 3), WSAEFAULT);
        FAILS(rt_net_getsockopt(s, SOL_SOCKET, SO_RCVBUF, NULL, &len), WSAEFAULT);
        FAILS(rt_net_getsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, NULL), WSAEFAULT);
        len = 0;
        FAILS(rt_net_getsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, &len), WSAEFAULT);    /* review NV19 */
        FAILS(rt_net_getsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &v, &len), WSAEFAULT);
        len = 3;
        v = 0x11223344;
        FAILS(rt_net_getsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, &len), WSAEFAULT);
        T_EQ(len, 3);
        T_EQ(v, 0x11223344);
        T_TRAPS(rt_net_setsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, -1), "rt_net_setsockopt: not implemented");
        len = -1;
        T_TRAPS(rt_net_getsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, &len), "rt_net_getsockopt: not implemented");
        T_TRAPS(rt_net_setsockopt(s, SOL_SOCKET, SO_BROADCAST, &v, 0), "rt_net_setsockopt: not implemented");
    }

    T_PLAN(7, "a level or an option the game was not seen to use is a stop that names both") {
        len = 4;
        T_TRAPS(rt_net_setsockopt(s, SOL_SOCKET, 0x7777, &v, 4), "rt_net_setsockopt: not implemented: this level and option (level 65535 = 0xffff, option 30583 = 0x7777");
        T_TRAPS(rt_net_getsockopt(s, SOL_SOCKET, 0x7777, &v, &len), "rt_net_getsockopt: not implemented");
        T_TRAPS(rt_net_setsockopt(s, 99, 1, &v, 4), "rt_net_setsockopt: not implemented");
        T_TRAPS(rt_net_getsockopt(s, 99, 1, &v, &len), "rt_net_getsockopt: not implemented");
        T_TRAPS(rt_net_setsockopt(s, IPPROTO_IP, 3, &v, 4), "rt_net_setsockopt: not implemented");
        v = -5;
        T_TRAPS(rt_net_setsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, 4), "rt_net_setsockopt: not implemented: a negative buffer size");
    }

    T_PLAN(8, "ioctlsocket") {
        char buf[4];
        SOCKET r = net_udp(AF_INET, -1, 46000, false);      /* blocking */
        SOCKADDR_IN to = v4(127, 0, 0, 1, 46000);
        for (ULONG *p = (ULONG[]){ 1, 0, 5, 99 }; *p != 99; p++) {
            LE_SET();
            T_EQ(rt_net_ioctl(r, FIONBIO, p), 0);           /* the game: 1, once per socket (8 calls recorded) */
            LE_UNTOUCHED();                                 /* this runtime's rule: rt/net.h, LAST ERROR */
        }
        FAILS(rt_net_recvfrom(r, buf, sizeof buf, 0, NULL, NULL), WSAEWOULDBLOCK);  /* 5 is "on" */
        arg = 99;
        T_EQ(rt_net_ioctl(r, FIONREAD, &arg), 0);
        T_EQ(arg, 0);
        T_EQ(rt_net_sendto(r, "abc", 3, 0, SA(to), sizeof to), 3);
        T_EQ(rt_net_sendto(r, "de", 2, 0, SA(to), sizeof to), 2);
        T_EQ(rt_net_ioctl(r, FIONREAD, &arg), 0);
        T_EQ(arg, 5);                                       /* the total of all datagrams queued */
        arg = 99;
        T_EQ(rt_net_ioctl(t, FIONREAD, &arg), 0);
        T_EQ(arg, 0);
        FAILS(rt_net_ioctl(r, FIONBIO, NULL), WSAEFAULT);
        FAILS(rt_net_ioctl(r, FIONREAD, NULL), WSAEFAULT);
        T_TRAPS(rt_net_ioctl(r, 0x12345678, &arg), "rt_net_ioctl: not implemented: command 0x12345678");
    }
}
