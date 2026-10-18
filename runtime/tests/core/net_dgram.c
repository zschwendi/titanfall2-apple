/* core/net_dgram.c: sendto, recvfrom, shutdown.  Specification: rt/net.h.
 * Where the expected values come from:
 *   - the codes and their order: the comments of rt_net_sendto, rt_net_recvfrom and
 *     rt_net_shutdown in rt/net.h, each of which names its source (Microsoft's pages of sendto,
 *     recvfrom and shutdown; Wine ws2_32/tests/sock.c; the reviews' rows NV21 to NV23, D22, D33);
 *   - the engine's cases (family, type, protocol, options, lengths 1264 and 1300, flags 0): the
 *     recording of the real game, docs/trace/net-calls.md sections 1 and 5;
 *   - who receives what, and the sender's address as the receiver sees it: THE MACHINE in
 *     rt/net.h (this machine's own rule, not a statement about Windows).
 * No host socket is opened and no packet is sent. */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

#include "net_test.h"

/* The next datagram of `s` is `text`, sent from `from_text` (in s's family) and `from_port`. */
static void gets_from(SOCKET s, int af, const char *text, const char *from_text, USHORT from_port) {
    char buf[64];
    SOCKADDR_IN6 from;
    memset(&from, 0xAA, sizeof from);
    int len = sizeof from;
    LE_SET();
    T_EQ(rt_net_recvfrom(s, buf, sizeof buf, 0, SA(from), &len), (int)strlen(text));
    LE_ZERO();                                              /* a receive that succeeds: last error 0 (D22) */
    T_EQ(memcmp(buf, text, strlen(text)), 0);
    T_EQ(len, af == AF_INET ? 16 : 28);
    T_EQ(from.sin6_family, af);
    T_EQ(port_of(&from), from_port);
    char got[65];
    const void *addr = af == AF_INET ? (const void *)&((SOCKADDR_IN *)&from)->sin_addr : (const void *)&from.sin6_addr;
    T_REQUIRE(rt_net_ntop(af, addr, got, sizeof got) != NULL);
    T_EQ(strcmp(got, from_text), 0);
}

static void gets_nothing(SOCKET s) {
    char buf[64];
    FAILS(rt_net_recvfrom(s, buf, sizeof buf, 0, NULL, NULL), WSAEWOULDBLOCK);
}

static int send4(SOCKET s, const char *text, BYTE a, BYTE b, BYTE c, BYTE d, USHORT port) {
    SOCKADDR_IN to = v4(a, b, c, d, port);
    return rt_net_sendto(s, text, (int)strlen(text), 0, SA(to), sizeof to);
}

static int send6(SOCKET s, const char *text, const char *addr, USHORT port) {
    SOCKADDR_IN6 to = v6(addr, port);
    return rt_net_sendto(s, text, (int)strlen(text), 0, SA(to), sizeof to);
}

TEST(core, net_dgram_engine)
{
    net_start();

    T_CASE("the engine's server socket as the recording has it: read empty a thousand times");
    SOCKET s = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);        /* socket(23, 2, 17) */
    ULONG on = 1;
    int size = 0x20000, zero = 0;
    T_EQ(rt_net_ioctl(s, FIONBIO, &on), 0);
    T_EQ(rt_net_setsockopt(s, SOL_SOCKET, SO_SNDBUF, &size, 4), 0);
    T_EQ(rt_net_setsockopt(s, SOL_SOCKET, SO_RCVBUF, &size, 4), 0);
    T_EQ(rt_net_setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4), 0);
    SOCKADDR_IN6 any = v6("::", 37015);
    T_EQ(rt_net_bind(s, SA(any), 28), 0);
    char buf[1264];
    SOCKADDR_IN6 from, untouched;
    memset(&from, 0xAA, sizeof from);
    untouched = from;
    int fromlen = 28;
    for (int i = 0; i < 1000; i++) {
        T_EQ(rt_net_recvfrom(s, buf, 1264, 0, SA(from), &fromlen), SOCKET_ERROR);
        T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);               /* the one code that ends the engine's loop */
    }
    T_EQ(memcmp(&from, &untouched, sizeof from), 0);
    T_EQ(fromlen, 28);

    T_CASE("the chat connection's socket: IPv4, bound to any port, read empty");
    SOCKET chat = rt_net_socket(AF_INET, SOCK_DGRAM, 0);                /* socket(2, 2, 0) */
    T_EQ(rt_net_ioctl(chat, FIONBIO, &on), 0);
    SOCKADDR_IN any4 = v4(0, 0, 0, 0, 0);
    T_EQ(rt_net_bind(chat, SA(any4), 16), 0);
    char big[1300];
    SOCKADDR_IN from4;
    fromlen = 16;
    FAILS(rt_net_recvfrom(chat, big, 1300, 0, SA(from4), &fromlen), WSAEWOULDBLOCK);

    T_CASE("a datagram to an address that is not this machine: no route, one line per destination");
    FAILS(send6(s, "ping", "2001:db8::1", 37015), WSAENETUNREACH);
    T_TRUE(T_LOGGED(RT_CH_NET, "sendto 2001:db8::1: no route"));
    unsigned lines = T_LOG_COUNT(RT_CH_NET);
    FAILS(send6(s, "ping", "2001:db8::1", 37016), WSAENETUNREACH);
    T_EQ(T_LOG_COUNT(RT_CH_NET), lines);
    FAILS(send6(s, "ping", "::ffff:192.0.2.1", 37015), WSAENETUNREACH);
    T_EQ(T_LOG_COUNT(RT_CH_NET), lines + 1);
    FAILS(send4(chat, "ping", 192, 0, 2, 1, 80), WSAENETUNREACH);

    T_CASE("its own port over loopback: delivered, with ::ffff:127.0.0.1 or ::1 as the sender");
    LE_SET();
    T_EQ(send6(s, "self4", "::ffff:127.0.0.1", 37015), 5);
    LE_ZERO();                                              /* a send that succeeds: last error 0 (D22) */
    T_EQ(send6(s, "self6", "::1", 37015), 5);
    gets_from(s, AF_INET6, "self4", "::ffff:127.0.0.1", 37015);
    gets_from(s, AF_INET6, "self6", "::1", 37015);
    gets_nothing(s);
    T_EQ(rt_net_close(s), 0);
    T_EQ(rt_net_close(chat), 0);
}

TEST(core, net_dgram_delivery)
{
    net_start();
    SOCKET r4 = net_udp(AF_INET, -1, 41004, true);          /* the three kinds of receiver */
    SOCKET rd = net_udp(AF_INET6, 0, 41006, true);
    SOCKET r6 = net_udp(AF_INET6, 1, 41066, true);
    SOCKET s4 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);      /* the three kinds of sender, not bound */
    SOCKET sd = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
    SOCKET s6 = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
    int zero = 0;
    T_EQ(rt_net_setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4), 0);

    T_CASE("the first send binds the sender to a port of the dynamic range");
    SOCKADDR_IN name;
    int len = sizeof name;
    FAILS(rt_net_getsockname(s4, SA(name), &len), WSAEINVAL);
    T_EQ(send4(s4, "a", 127, 0, 0, 1, 41004), 1);
    USHORT p4 = net_port(s4);
    T_TRUE(p4 >= 49152);
    gets_from(r4, AF_INET, "a", "127.0.0.1", p4);

    T_CASE("an IPv4 sender: an IPv4 socket and a dual socket take it, an IPv6-only socket does not");
    T_EQ(send4(s4, "b", 127, 0, 0, 1, 41006), 1);
    gets_from(rd, AF_INET6, "b", "::ffff:127.0.0.1", p4);
    T_EQ(send4(s4, "c", 127, 0, 0, 1, 41066), 1);           /* dropped: the result is still the length */
    gets_nothing(r6);
    T_EQ(send4(s4, "d", 127, 9, 9, 9, 41004), 1);           /* all of 127.0.0.0/8 is this machine */
    gets_from(r4, AF_INET, "d", "127.0.0.1", p4);

    T_CASE("a dual IPv6 sender over ::1");
    T_EQ(send6(sd, "e", "::1", 41006), 1);
    USHORT pd = net_port(sd);
    T_TRUE(pd >= 49152 && pd != p4);
    gets_from(rd, AF_INET6, "e", "::1", pd);
    T_EQ(send6(sd, "f", "::1", 41066), 1);
    gets_from(r6, AF_INET6, "f", "::1", pd);
    T_EQ(send6(sd, "g", "::1", 41004), 1);                  /* an IPv4 socket does not take ::1 */
    gets_nothing(r4);

    T_CASE("a dual IPv6 sender over ::ffff:127.0.0.1");
    T_EQ(send6(sd, "h", "::ffff:127.0.0.1", 41004), 1);
    gets_from(r4, AF_INET, "h", "127.0.0.1", pd);
    T_EQ(send6(sd, "i", "::ffff:127.0.0.1", 41006), 1);
    gets_from(rd, AF_INET6, "i", "::ffff:127.0.0.1", pd);
    T_EQ(send6(sd, "j", "::ffff:127.0.0.1", 41066), 1);
    gets_nothing(r6);

    T_CASE("an IPv6-only sender over ::1; an IPv4-mapped destination there is a stop");
    T_EQ(send6(s6, "k", "::1", 41066), 1);
    USHORT p6 = net_port(s6);
    gets_from(r6, AF_INET6, "k", "::1", p6);
    T_EQ(send6(s6, "l", "::1", 41006), 1);
    gets_from(rd, AF_INET6, "l", "::1", p6);
    T_TRAPS(send6(s6, "m", "::ffff:127.0.0.1", 41006), "rt_net_sendto: not implemented");

    T_CASE("the order of one sender is kept; an empty datagram is a datagram");
    T_EQ(send4(s4, "1", 127, 0, 0, 1, 41004), 1);
    T_EQ(send4(s4, "", 127, 0, 0, 1, 41004), 0);
    T_EQ(send4(s4, "3", 127, 0, 0, 1, 41004), 1);
    ULONG queued = 99;
    T_EQ(rt_net_ioctl(r4, FIONREAD, &queued), 0);
    T_EQ(queued, 2);                                        /* the bytes of all datagrams queued */
    gets_from(r4, AF_INET, "1", "127.0.0.1", p4);
    gets_from(r4, AF_INET, "", "127.0.0.1", p4);
    gets_from(r4, AF_INET, "3", "127.0.0.1", p4);
    gets_nothing(r4);

    T_CASE("a datagram longer than the buffer: its start, WSAEMSGSIZE, and the rest is lost");
    T_EQ(send4(s4, "0123456789", 127, 0, 0, 1, 41004), 10);
    char small[4] = { 0 };
    SOCKADDR_IN from;
    len = sizeof from;
    FAILS(rt_net_recvfrom(r4, small, 4, 0, SA(from), &len), WSAEMSGSIZE);
    T_EQ(memcmp(small, "0123", 4), 0);
    T_EQ(port_of(&from), p4);
    gets_nothing(r4);

    T_CASE("nobody has the port, and port 0: the length, and nothing happens");
    T_EQ(send4(s4, "x", 127, 0, 0, 1, 41999), 1);
    SOCKET fresh = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    char sixteen[16] = { 0 };
    SOCKADDR_IN port0 = v4(127, 0, 0, 1, 0);
    T_EQ(rt_net_sendto(fresh, sixteen, 16, 0, SA(port0), sizeof port0), 16);    /* review D33 */
    T_TRUE(net_port(fresh) >= 49152);
    T_EQ(send6(sd, "x", "::1", 0), 1);

    T_CASE("the unspecified address is no destination");
    FAILS(send4(s4, "x", 0, 0, 0, 0, 41004), WSAEADDRNOTAVAIL);
    FAILS(send6(sd, "x", "::", 41006), WSAEADDRNOTAVAIL);

    for (SOCKET *p = (SOCKET[]){ r4, rd, r6, s4, sd, s6, fresh, 0 }; *p; p++) T_EQ(rt_net_close(*p), 0);
    T_EQ(rt_net_test_open_count(), 0);
}

TEST(core, net_dgram_limits)
{
    net_start();

    T_CASE("the queue holds SO_RCVBUF bytes: a datagram that does not fit is dropped");
    SOCKET r = net_udp(AF_INET, -1, 42000, true);
    SOCKET s = net_udp(AF_INET, -1, 42001, true);
    int limit = 100;
    T_EQ(rt_net_setsockopt(r, SOL_SOCKET, SO_RCVBUF, &limit, 4), 0);
    char sixty[60];
    memset(sixty, 'q', sizeof sixty);
    SOCKADDR_IN to = v4(127, 0, 0, 1, 42000);
    T_EQ(rt_net_sendto(s, sixty, 60, 0, SA(to), sizeof to), 60);
    T_EQ(rt_net_sendto(s, sixty, 60, 0, SA(to), sizeof to), 60);        /* 120 > 100: dropped */
    T_EQ(rt_net_sendto(s, sixty, 40, 0, SA(to), sizeof to), 40);        /* exactly 100: kept */
    ULONG queued = 0;
    T_EQ(rt_net_ioctl(r, FIONREAD, &queued), 0);
    T_EQ(queued, 100);
    char buf[128];
    T_EQ(rt_net_recvfrom(r, buf, sizeof buf, 0, NULL, NULL), 60);
    T_EQ(rt_net_recvfrom(r, buf, sizeof buf, 0, NULL, NULL), 40);
    gets_nothing(r);

    T_CASE("the largest datagram is 65507 bytes");
    static char huge[65508];
    limit = 0x20000;
    T_EQ(rt_net_setsockopt(r, SOL_SOCKET, SO_RCVBUF, &limit, 4), 0);
    T_EQ(rt_net_sendto(s, huge, 65507, 0, SA(to), sizeof to), 65507);
    FAILS(rt_net_sendto(s, huge, 65508, 0, SA(to), sizeof to), WSAEMSGSIZE);
    static char back[65507];
    T_EQ(rt_net_recvfrom(r, back, sizeof back, 0, NULL, NULL), 65507);

    T_CASE("a connected socket takes datagrams from its peer only, and send goes to the peer");
    SOCKADDR_IN peer = v4(127, 0, 0, 1, 42001);
    FAILS(rt_net_sendto(r, "x", 1, 0, NULL, 0), WSAENOTCONN);           /* send on a socket with no peer */
    T_EQ(rt_net_connect(r, SA(peer), sizeof peer), 0);
    SOCKET stranger = net_udp(AF_INET, -1, 42002, true);
    T_EQ(rt_net_sendto(stranger, "no", 2, 0, SA(to), sizeof to), 2);
    T_EQ(rt_net_sendto(s, "yes", 3, 0, SA(to), sizeof to), 3);
    gets_from(r, AF_INET, "yes", "127.0.0.1", 42001);
    gets_nothing(r);
    LE_SET();
    T_EQ(rt_net_sendto(r, "back", 4, 0, NULL, 0), 4);
    LE_ZERO();
    gets_from(s, AF_INET, "back", "127.0.0.1", 42000);
    T_TRAPS(rt_net_sendto(r, "x", 1, 0, SA(peer), sizeof peer), "rt_net_sendto: not implemented");

    T_CASE("a stream socket is never connected");
    SOCKET t = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    FAILS(rt_net_sendto(t, "x", 1, 0, NULL, 0), WSAENOTCONN);
    FAILS(rt_net_sendto(t, "x", 1, 0, SA(to), sizeof to), WSAENOTCONN);  /* `to` is ignored */
    FAILS(rt_net_recvfrom(t, buf, sizeof buf, 0, NULL, NULL), WSAENOTCONN);
    FAILS(rt_net_shutdown(t, SD_BOTH), WSAENOTCONN);

    T_CASE("a socket closed with datagrams queued");
    T_EQ(rt_net_sendto(s, sixty, 60, 0, SA(to), sizeof to), 60);
    T_EQ(rt_net_close(r), 0);
    T_EQ(rt_net_sendto(s, sixty, 60, 0, SA(to), sizeof to), 60);        /* nobody has the port now */
    T_EQ(rt_net_close(s), 0);
    T_EQ(rt_net_close(stranger), 0);
    T_EQ(rt_net_close(t), 0);
    T_EQ(rt_net_test_open_count(), 0);
}

TEST(core, net_dgram_errors)
{
    char buf[32];
    SOCKADDR_IN to = v4(127, 0, 0, 1, 43000), from;
    int len = sizeof from;

    T_CASE("before any start");
    FAILS(rt_net_sendto(4, buf, 1, 0, SA(to), sizeof to), WSANOTINITIALISED);
    FAILS(rt_net_recvfrom(4, buf, 1, 0, NULL, NULL), WSANOTINITIALISED);
    FAILS(rt_net_shutdown(4, SD_BOTH), WSANOTINITIALISED);
    net_start();

    T_CASE("not a socket");
    SOCKET gone = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_EQ(rt_net_close(gone), 0);
    for (SOCKET *p = (SOCKET[]){ gone, 0x100000004ull, 0 }; ; p++) {
        FAILS(rt_net_sendto(*p, buf, 1, 0, SA(to), sizeof to), WSAENOTSOCK);
        FAILS(rt_net_recvfrom(*p, buf, 1, 0, NULL, NULL), WSAENOTSOCK);
        FAILS(rt_net_shutdown(*p, SD_BOTH), WSAENOTSOCK);
        if (!*p) break;
    }

    T_CASE("sendto's arguments");
    SOCKET s = net_udp(AF_INET, -1, 43000, true);
    SOCKET s6 = net_udp(AF_INET6, 0, 43006, true);
    SOCKADDR_IN6 to6 = v6("::1", 43006);
    FAILS(rt_net_sendto(s, buf, -1, 0, SA(to), sizeof to), WSAEFAULT);
    FAILS(rt_net_sendto(s, NULL, 1, 0, SA(to), sizeof to), WSAEFAULT);
    T_EQ(rt_net_sendto(s, NULL, 0, 0, SA(to), sizeof to), 0);           /* no bytes, no buffer needed */
    FAILS(rt_net_sendto(s, buf, 1, 0, SA(to), 15), WSAEFAULT);
    FAILS(rt_net_sendto(s6, buf, 1, 0, SA(to6), 27), WSAEFAULT);
    T_TRAPS(rt_net_sendto(s, buf, 1, MSG_OOB, SA(to), sizeof to), "rt_net_sendto: not implemented");
    T_TRAPS(rt_net_sendto(s6, buf, 1, 0, SA(to), 28), "rt_net_sendto: not implemented");    /* an AF_INET name on an AF_INET6 socket */

    T_CASE("recvfrom's arguments");
    T_EQ(rt_net_recvfrom(s, buf, sizeof buf, 0, NULL, NULL), 0);        /* the empty datagram sent above */
    FAILS(rt_net_recvfrom(s, buf, 0, 0, NULL, NULL), WSAEFAULT);
    FAILS(rt_net_recvfrom(s, NULL, 4, 0, NULL, NULL), WSAEFAULT);
    FAILS(rt_net_recvfrom(s, buf, 4, 0, SA(from), NULL), WSAEFAULT);
    len = 15;
    FAILS(rt_net_recvfrom(s, buf, 4, 0, SA(from), &len), WSAEFAULT);
    T_EQ(len, 15);
    T_TRAPS(rt_net_recvfrom(s, buf, 4, MSG_PEEK, NULL, NULL), "rt_net_recvfrom: not implemented");
    SOCKET unbound = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    FAILS(rt_net_recvfrom(unbound, buf, 4, 0, NULL, NULL), WSAEINVAL);

    T_CASE("a blocking receive with nothing queued is a stop");
    SOCKET blocking = net_udp(AF_INET, -1, 43001, false);
    T_TRAPS(rt_net_recvfrom(blocking, buf, 4, 0, NULL, NULL), "rt_net_recvfrom: not implemented: blocking receive");
    SOCKADDR_IN tob = v4(127, 0, 0, 1, 43001);
    T_EQ(rt_net_sendto(s, "hi", 2, 0, SA(tob), sizeof tob), 2);
    T_EQ(rt_net_recvfrom(blocking, buf, 4, 0, NULL, NULL), 2);          /* with something queued it returns */

    T_CASE("shutdown");
    FAILS(rt_net_shutdown(s, 3), WSAEINVAL);
    LE_SET();
    T_EQ(rt_net_shutdown(s, SD_SEND), 0);
    LE_ZERO();                                              /* a shutdown that succeeds: last error 0 (D22) */
    FAILS(rt_net_sendto(s, buf, 1, 0, SA(to), sizeof to), WSAESHUTDOWN);
    T_EQ(rt_net_sendto(s6, "in", 2, 0, SA(to6), sizeof to6), 2);
    T_EQ(rt_net_sendto(blocking, "in", 2, 0, SA(to), sizeof to), 2);    /* s still receives */
    T_EQ(rt_net_recvfrom(s, buf, sizeof buf, 0, NULL, NULL), 2);
    T_EQ(rt_net_shutdown(s6, SD_RECEIVE), 0);               /* what is queued is dropped */
    ULONG queued = 99;
    T_EQ(rt_net_ioctl(s6, FIONREAD, &queued), 0);
    T_EQ(queued, 0);
    FAILS(rt_net_recvfrom(s6, buf, sizeof buf, 0, NULL, NULL), WSAESHUTDOWN);
    T_EQ(rt_net_sendto(s6, "in", 2, 0, SA(to6), sizeof to6), 2);        /* it still sends; to itself: dropped */
    T_EQ(rt_net_ioctl(s6, FIONREAD, &queued), 0);
    T_EQ(queued, 0);
    T_EQ(rt_net_shutdown(blocking, SD_BOTH), 0);
    FAILS(rt_net_sendto(blocking, buf, 1, 0, SA(to), sizeof to), WSAESHUTDOWN);
    FAILS(rt_net_recvfrom(blocking, buf, sizeof buf, 0, NULL, NULL), WSAESHUTDOWN);
}

/* 8 senders, 500 datagrams each, one receiver that reads while they send: every datagram
 * arrives once, and each sender's in the order it sent them. */
enum { SENDERS = 8, PER_SENDER = 500, RX_PORT = 44000 };
static atomic_uint send_bad;

static void *sender(void *arg) {
    unsigned id = (unsigned)(uintptr_t)arg;
    SOCKET s = rt_net_socket(AF_INET6, SOCK_DGRAM, 0);
    SOCKADDR_IN6 to = { .sin6_family = AF_INET6, .sin6_port = (USHORT)((RX_PORT >> 8) | ((RX_PORT & 0xff) << 8)) };
    to.sin6_addr.s6_addr[15] = 1;
    for (unsigned i = 0; i < PER_SENDER; i++) {
        unsigned msg[2] = { id, i };
        if (rt_net_sendto(s, msg, sizeof msg, 0, SA(to), sizeof to) != (int)sizeof msg) atomic_fetch_add(&send_bad, 1);
    }
    if (rt_net_close(s) != 0) atomic_fetch_add(&send_bad, 1);
    return NULL;
}

TEST(core, net_dgram_threads)
{
    T_CASE("8 senders and one receiver lose and duplicate nothing");
    net_start();
    SOCKET r = net_udp(AF_INET6, 1, RX_PORT, true);         /* 8 * 500 * 8 bytes is below the default SO_RCVBUF */
    pthread_t t[SENDERS];
    for (uintptr_t i = 0; i < SENDERS; i++) T_EQ(pthread_create(&t[i], NULL, sender, (void *)i), 0);
    unsigned next[SENDERS] = { 0 }, got = 0, wrong = 0, idle = 0;
    while (got < SENDERS * PER_SENDER && idle < 20000000) {
        unsigned msg[2];
        int n = rt_net_recvfrom(r, msg, sizeof msg, 0, NULL, NULL);
        if (n == SOCKET_ERROR) { idle++; sched_yield(); continue; }
        if (n != (int)sizeof msg || msg[0] >= SENDERS || msg[1] != next[msg[0]]) wrong++;
        else next[msg[0]]++;
        got++;
    }
    for (int i = 0; i < SENDERS; i++) pthread_join(t[i], NULL);
    T_EQ(got, SENDERS * PER_SENDER);
    T_EQ(wrong, 0);
    T_EQ(atomic_load(&send_bad), 0);
    gets_nothing(r);
    T_EQ(rt_net_close(r), 0);
    T_EQ(rt_net_test_open_count(), 0);
}
