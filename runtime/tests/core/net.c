/* core/net.c: Winsock's start count, the socket object and its lifetime, the text helpers.
 * Specification: rt/net.h.  Where the expected values come from:
 *   - WSAStartup's versions: the table on Microsoft's page of WSAStartup (read 2026-10-10), and
 *     Wine ws2_32/tests/protocol.c test_startup (review Z5);
 *   - IPv6 text: the examples of RFC 4291 section 2.2 and RFC 5952 section 4;
 *   - inet_addr: the four forms on Microsoft's page of inet_addr, worked out by hand;
 *   - socket values: docs/RUNTIME.md section 4.1 (a multiple of 4, never 0, never negative).
 * No host socket is opened and no packet is sent. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/handle.h"
#include "rt/net.h"
#include "rt/thread.h"

unsigned rt_net_test_open_count(void);
unsigned rt_net_test_start_count(void);

#define LE_SET()        rt_thread_set_last_error(0xDEAD)
#define LE_UNTOUCHED()  T_EQ(T_LAST_ERROR(), 0xDEAD)

static void start(void) {
    WSADATA d;
    T_EQ(rt_net_startup(0x0202, &d), 0);
}

/* The fields rt_net_startup writes (Wine ws2_32/tests/protocol.c, test_startup; review Z5): the
 * version, the high version 2.2, both texts, and the limits 32767 and 65467 for a major version
 * 1, 0 otherwise.  lpVendorInfo is not written: fresh() presets it and check_written() checks it. */
#define PRESET_VENDOR  ((char *)(uintptr_t)0xdeadbeef)

static void fresh(WSADATA *d) {
    memset(d, 0xAA, sizeof *d);
    d->lpVendorInfo = PRESET_VENDOR;
}

static void check_written(const WSADATA *d, WORD version, unsigned max_sockets, unsigned max_udp) {
    T_EQ(d->wVersion, version);
    T_EQ(d->wHighVersion, 0x0202);
    T_EQ(strcmp(d->szDescription, "WinSock 2.0"), 0);
    T_EQ(strcmp(d->szSystemStatus, "Running"), 0);
    T_EQ(d->iMaxSockets, max_sockets);
    T_EQ(d->iMaxUdpDg, max_udp);
    T_TRUE(d->lpVendorInfo == PRESET_VENDOR);
}

TEST(core, net_startup)
{
    WSADATA d;

    T_CASE("2.2 asked, 2.2 given");
    fresh(&d);
    LE_SET();
    T_EQ(rt_net_startup(0x0202, &d), 0);
    LE_UNTOUCHED();
    check_written(&d, 0x0202, 0, 0);
    T_EQ(d.szDescription[WSADESCRIPTION_LEN], 0);          /* zero-filled behind the text */
    T_EQ(rt_net_test_start_count(), 1);

    T_CASE("1.0 and 1.1 are given as asked, with the 1.x limits");
    fresh(&d);
    T_EQ(rt_net_startup(0x0001, &d), 0);                    /* 1.0 */
    check_written(&d, 0x0001, 32767, 65467);
    fresh(&d);
    T_EQ(rt_net_startup(0x0101, &d), 0);                    /* 1.1 */
    check_written(&d, 0x0101, 32767, 65467);
    T_EQ(rt_net_test_start_count(), 3);

    T_CASE("2.0 and 2.1 are given as asked, with both limits 0");
    fresh(&d);
    T_EQ(rt_net_startup(0x0002, &d), 0);                    /* 2.0 */
    check_written(&d, 0x0002, 0, 0);
    fresh(&d);
    T_EQ(rt_net_startup(0x0102, &d), 0);                    /* 2.1 */
    check_written(&d, 0x0102, 0, 0);
    T_EQ(rt_net_test_start_count(), 5);

    T_CASE("1.2 to 1.255 are given as 1.1, with the 1.x limits");
    fresh(&d);
    T_EQ(rt_net_startup(0x0201, &d), 0);                    /* 1.2 */
    check_written(&d, 0x0101, 32767, 65467);
    fresh(&d);
    T_EQ(rt_net_startup(0x0501, &d), 0);                    /* 1.5 */
    check_written(&d, 0x0101, 32767, 65467);
    fresh(&d);
    T_EQ(rt_net_startup(0xFF01, &d), 0);                    /* 1.255 */
    check_written(&d, 0x0101, 32767, 65467);
    T_EQ(rt_net_test_start_count(), 8);

    T_CASE("a higher version gets 2.2, with both limits 0");
    fresh(&d);
    T_EQ(rt_net_startup(0x0003, &d), 0);                    /* 3.0 */
    check_written(&d, 0x0202, 0, 0);
    fresh(&d);
    T_EQ(rt_net_startup(0x0302, &d), 0);                    /* 2.3 */
    check_written(&d, 0x0202, 0, 0);
    fresh(&d);
    T_EQ(rt_net_startup(0xFF02, &d), 0);                    /* 2.255 */
    check_written(&d, 0x0202, 0, 0);
    fresh(&d);
    T_EQ(rt_net_startup(0x00FF, &d), 0);                    /* 255.0 */
    check_written(&d, 0x0202, 0, 0);
    T_EQ(rt_net_test_start_count(), 12);

    T_CASE("errors are returned, not set; a major version 0 still fills the structure");
    LE_SET();
    T_EQ(rt_net_startup(0x0202, NULL), WSAEFAULT);
    fresh(&d);
    T_EQ(rt_net_startup(0x0200, &d), WSAVERNOTSUPPORTED);   /* 0.2 */
    check_written(&d, 0x0202, 0, 0);
    fresh(&d);
    T_EQ(rt_net_startup(0x0100, &d), WSAVERNOTSUPPORTED);   /* 0.1 */
    check_written(&d, 0x0202, 0, 0);
    T_EQ(rt_net_startup(0x0000, NULL), WSAVERNOTSUPPORTED); /* the version before the pointer: Wine protocol.c test_startup */
    LE_UNTOUCHED();
    T_EQ(rt_net_test_start_count(), 12);                    /* no failure counted as a start */

    T_CASE("one cleanup per start");
    LE_SET();
    for (int i = 0; i < 12; i++) T_EQ(rt_net_cleanup(), 0);
    T_EQ(T_LAST_ERROR(), 0);                                /* a cleanup that succeeds resets it: Wine protocol.c test_startup */
    T_EQ(rt_net_test_start_count(), 0);
    T_EQ(rt_net_cleanup(), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);
}

TEST(core, net_socket)
{
    T_CASE("nothing before the start");
    T_TRUE(rt_net_socket(AF_INET, SOCK_DGRAM, 0) == INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);
    T_EQ(rt_net_close(4), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);
    start();

    T_CASE("the four kinds the game makes");
    LE_SET();
    SOCKET a = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);   /* engine+0x21ac0d */
    SOCKET b = rt_net_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);  /* engine+0x21abfd */
    SOCKET c = rt_net_socket(AF_INET, SOCK_DGRAM, 0);              /* engine+0x1f27e2 */
    SOCKET d = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_EQ(T_LAST_ERROR(), 0);                                       /* success resets it: Wine sock.c test_WSASocket */
    SOCKET all[4] = { a, b, c, d };
    for (int i = 0; i < 4; i++) {
        T_TRUE(all[i] != INVALID_SOCKET && all[i] != 0);
        T_EQ(all[i] & 3, 0);
        T_EQ(all[i] >> 31, 0);                             /* positive as the int the game keeps it in */
        for (int j = 0; j < i; j++) T_TRUE(all[i] != all[j]);
    }
    T_EQ(rt_net_test_open_count(), 4);

    T_CASE("refused arguments, in order: type, then family, then protocol (Wine sock.c, test_WSASocket)");
    T_TRUE(rt_net_socket(0xdead, 0xdead, IPPROTO_TCP) == INVALID_SOCKET);   /* the type before the family */
    T_EQ(T_LAST_ERROR(), WSAESOCKTNOSUPPORT);
    T_TRUE(rt_net_socket(0xdead, SOCK_STREAM, 0xdead) == INVALID_SOCKET);   /* the family before the protocol */
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_TRUE(rt_net_socket(AF_UNSPEC, 99, 99) == INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAESOCKTNOSUPPORT);
    T_TRUE(rt_net_socket(1, SOCK_DGRAM, 0) == INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_TRUE(rt_net_socket(AF_INET, 3, 99) == INVALID_SOCKET);       /* SOCK_RAW */
    T_EQ(T_LAST_ERROR(), WSAESOCKTNOSUPPORT);
    T_TRUE(rt_net_socket(AF_INET, 0, 99) == INVALID_SOCKET);       /* type 0 takes only 0, IPPROTO_TCP, IPPROTO_UDP */
    T_EQ(T_LAST_ERROR(), WSAEPROTONOSUPPORT);
    T_TRUE(rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_TCP) == INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEPROTONOSUPPORT);
    T_TRUE(rt_net_socket(AF_INET6, SOCK_STREAM, IPPROTO_UDP) == INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEPROTONOSUPPORT);
    T_EQ(rt_net_test_open_count(), 4);

    T_CASE("type 0 and AF_UNSPEC as Windows gives them");
    T_TRUE(rt_net_socket(AF_UNSPEC, SOCK_DGRAM, 0) == INVALID_SOCKET);   /* protocol 0: WSAEINVAL whatever the type */
    T_EQ(T_LAST_ERROR(), WSAEINVAL);
    T_TRUE(rt_net_socket(AF_UNSPEC, 0, 0) == INVALID_SOCKET);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);
    LE_SET();
    SOCKET e = rt_net_socket(AF_INET, 0, IPPROTO_UDP);             /* a datagram socket (SO_TYPE: rt_net_getsockopt is not built) */
    SOCKET f = rt_net_socket(AF_INET, 0, 0);                       /* a stream socket */
    SOCKET g = rt_net_socket(AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);  /* an AF_INET datagram socket */
    SOCKET h = rt_net_socket(AF_UNSPEC, 0, IPPROTO_TCP);           /* an AF_INET stream socket */
    T_EQ(T_LAST_ERROR(), 0);                                       /* success resets it: Wine sock.c test_WSASocket */
    T_TRUE(e != INVALID_SOCKET && f != INVALID_SOCKET && g != INVALID_SOCKET && h != INVALID_SOCKET);
    T_EQ(rt_net_test_open_count(), 8);
    T_EQ(rt_net_close(e), 0);
    T_EQ(rt_net_close(f), 0);
    T_EQ(rt_net_close(g), 0);
    T_EQ(rt_net_close(h), 0);
    T_EQ(rt_net_test_open_count(), 4);

    T_CASE("CloseHandle does not close a socket");
    T_EQ(rt_handle_close((HANDLE)(uintptr_t)a), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_net_test_open_count(), 4);

    T_CASE("closesocket");
    LE_SET();
    T_EQ(rt_net_close(a), 0);
    LE_UNTOUCHED();
    T_EQ(rt_net_test_open_count(), 3);
    T_EQ(rt_net_close(a), SOCKET_ERROR);                   /* twice */
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(rt_net_close(0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(rt_net_close(INVALID_SOCKET), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(rt_net_close(b | (SOCKET)1 << 32), SOCKET_ERROR); /* bits above 31 */
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(rt_net_test_open_count(), 3);
    HANDLE ev = rt_handle_dup((HANDLE)-2);                 /* a handle of another kind: a thread */
    T_TRUE(ev != NULL);
    T_EQ(rt_net_close((SOCKET)(uintptr_t)ev), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_EQ(rt_handle_close(ev), TRUE);                       /* and it was not closed */

    T_CASE("the last cleanup keeps the sockets and says so");
    T_EQ(rt_net_cleanup(), 0);
    T_TRUE(T_LOGGED(RT_CH_NET, "WSACleanup: the last one, with 3 socket(s) still open"));
    T_EQ(rt_net_test_open_count(), 3);
    T_EQ(rt_net_close(b), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);
    start();
    T_EQ(rt_net_close(b), 0);
    T_EQ(rt_net_close(c), 0);
    T_EQ(rt_net_close(d), 0);
    T_EQ(rt_net_test_open_count(), 0);
}

static atomic_uint bad;

static void *churn(void *arg) {
    (void)arg;
    for (int i = 0; i < 200; i++) {
        SOCKET s = rt_net_socket(i & 1 ? AF_INET : AF_INET6, SOCK_DGRAM, 0);
        if (s == INVALID_SOCKET || s == 0 || (s & 3) || rt_net_close(s) != 0) atomic_fetch_add(&bad, 1);
    }
    return NULL;
}

TEST(core, net_socket_threads)
{
    T_CASE("8 threads make and close 200 sockets each");
    start();
    pthread_t t[8];
    for (int i = 0; i < 8; i++) T_EQ(pthread_create(&t[i], NULL, churn, NULL), 0);
    for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
    T_EQ(atomic_load(&bad), 0);
    T_EQ(rt_net_test_open_count(), 0);
}

static void pton6_is(const char *text, const char *hex) {
    BYTE got[16], want[16];
    for (int i = 0; i < 16; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char ch = hex[2 * i + k];
            v = v * 16 + (unsigned)(ch <= '9' ? ch - '0' : ch - 'a' + 10);
        }
        want[i] = (BYTE)v;
    }
    memset(got, 0xAA, sizeof got);
    T_EQ(rt_net_pton(AF_INET6, text, got), 1);
    T_EQ(memcmp(got, want, 16), 0);
}

TEST(core, net_pton)
{
    BYTE b[17];

    T_CASE("IPv4, strict dotted decimal");
    memset(b, 0xAA, sizeof b);
    LE_SET();
    T_EQ(rt_net_pton(AF_INET, "127.0.0.1", b), 1);
    T_TRUE(b[0] == 127 && b[1] == 0 && b[2] == 0 && b[3] == 1 && b[4] == 0xAA);
    T_EQ(rt_net_pton(AF_INET, "255.255.255.255", b), 1);
    T_TRUE(b[0] == 255 && b[3] == 255);
    T_EQ(rt_net_pton(AF_INET, "0.0.0.0", b), 1);
    T_TRUE(b[0] == 0 && b[3] == 0);
    memset(b, 0xAA, sizeof b);
    /* A number with a leading zero is not an address (Wine ws2_32/tests/protocol.c,
     * test_inet_pton, the IPv4 table: "1.1.1.010" and "1.1.1.00" give 0; win-behaviour-check-5 Z6). */
    const char *no4[] = { "", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.4 ", " 1.2.3.4", "1..2.3", "a.b.c.d",
                          "0x7f.0.0.1", "1.2.3.", "1234.1.1.1", "::1", "01.2.3.4", "1.1.1.010", "1.1.1.00",
                          "1.1.1.0a", "1.1.1.1%16", "%1.1.1.1" };
    for (unsigned i = 0; i < sizeof no4 / sizeof *no4; i++) T_EQ(rt_net_pton(AF_INET, no4[i], b), 0);
    T_TRUE(b[0] == 0xAA && b[3] == 0xAA);                   /* nothing stored */
    LE_UNTOUCHED();

    T_CASE("IPv6, the forms of RFC 4291 section 2.2");
    LE_SET();
    pton6_is("ABCD:EF01:2345:6789:ABCD:EF01:2345:6789", "abcdef0123456789abcdef0123456789");
    pton6_is("2001:DB8:0:0:8:800:200C:417A", "20010db80000000000080800200c417a");
    pton6_is("2001:DB8::8:800:200C:417A", "20010db80000000000080800200c417a");
    pton6_is("FF01::101", "ff010000000000000000000000000101");
    pton6_is("::1", "00000000000000000000000000000001");
    pton6_is("::", "00000000000000000000000000000000");
    pton6_is("1::", "00010000000000000000000000000000");
    pton6_is("0:0:0:0:0:0:13.1.68.3", "0000000000000000000000000d014403");
    pton6_is("::FFFF:129.144.52.38", "00000000000000000000ffff81903426");
    pton6_is("::FFFF:127.0.0.1", "00000000000000000000ffff7f000001");   /* engine+0x41f9de */
    pton6_is("1:2:3:4:5:6:7::", "00010002000300040005000600070000");
    /* The dotted tail takes one to three digits per number, leading zeros allowed there
     * (Wine test_inet_pton, the IPv6 table; win-behaviour-check-5 Z6). */
    pton6_is("1::001.2.3.4", "00010000000000000000000001020304");
    pton6_is("1::1.002.3.4", "00010000000000000000000001020304");
    memset(b, 0xAA, sizeof b);
    const char *no6[] = { "", ":", ":1", "1:", "1:::2", "1::2::3", "1:2:3:4:5:6:7", "1:2:3:4:5:6:7:8:9",
                          "1:2:3:4:5:6:7:8::", "12345::", "g::", "[::1]", "::1.2.3", "1:2:3:4:5:6:7:1.2.3.4",
                          "127.0.0.1", "::1 ", "1::0001.2.3.4", "1::1.2.3.0004", "::%16", "fe80::1%1" };
    for (unsigned i = 0; i < sizeof no6 / sizeof *no6; i++) T_EQ(rt_net_pton(AF_INET6, no6[i], b), 0);
    T_TRUE(b[0] == 0xAA && b[15] == 0xAA);
    LE_UNTOUCHED();

    T_CASE("errors");
    T_EQ(rt_net_pton(AF_UNSPEC, "1.2.3.4", b), -1);         /* a valid text, a bad family */
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_EQ(rt_net_pton(AF_INET, NULL, b), -1);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    rt_thread_set_last_error(0);
    T_EQ(rt_net_pton(AF_INET6, "::1", NULL), -1);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    rt_thread_set_last_error(0);
    T_EQ(rt_net_pton(AF_UNSPEC, NULL, b), -1);              /* a NULL text is looked at before the family */
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
}

static void ntop6_is(const char *in, const char *want) {
    BYTE a[16];
    char buf[INET6_ADDRSTRLEN];
    T_EQ(rt_net_pton(AF_INET6, in, a), 1);
    T_TRUE(rt_net_ntop(AF_INET6, a, buf, sizeof buf) == buf);
    T_EQ(strcmp(buf, want), 0);
}

TEST(core, net_ntop)
{
    char buf[INET6_ADDRSTRLEN + 1];
    BYTE v4[4] = { 192, 168, 0, 1 }, any[16] = { 0 };

    T_CASE("IPv4");
    LE_SET();
    T_TRUE(rt_net_ntop(AF_INET, v4, buf, sizeof buf) == buf);
    T_EQ(strcmp(buf, "192.168.0.1"), 0);
    T_TRUE(rt_net_ntop(AF_INET, v4, buf, 12) == buf);       /* exactly the text and its terminator */
    LE_UNTOUCHED();
    memset(buf, 'x', sizeof buf);
    T_TRUE(rt_net_ntop(AF_INET, v4, buf, 11) == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);   /* Wine protocol.c test_addr_to_print */
    T_EQ(buf[0], 'x');

    T_CASE("IPv6, the form of RFC 5952 section 4");
    LE_SET();
    ntop6_is("2001:0DB8:0:0:0:0:0:1", "2001:db8::1");
    ntop6_is("2001:DB8:0:0:8:800:200C:417A", "2001:db8::8:800:200c:417a");
    ntop6_is("2001:db8:0:0:1:0:0:1", "2001:db8::1:0:0:1");   /* the first of two equal runs */
    ntop6_is("2001:0:0:1:0:0:0:1", "2001:0:0:1::1");         /* the longest run */
    ntop6_is("ABCD:EF01:2345:6789:ABCD:EF01:2345:6789", "abcd:ef01:2345:6789:abcd:ef01:2345:6789");
    ntop6_is("FF01::101", "ff01::101");
    ntop6_is("1::", "1::");
    ntop6_is("::1", "::1");
    ntop6_is("::", "::");
    ntop6_is("::FFFF:129.144.52.38", "::ffff:129.144.52.38");
    LE_UNTOUCHED();
    T_TRUE(rt_net_ntop(AF_INET6, any, buf, 3) == buf);
    T_TRUE(rt_net_ntop(AF_INET6, any, buf, 2) == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);   /* Wine protocol.c test_addr_to_print */

    T_CASE("errors");
    T_TRUE(rt_net_ntop(AF_UNSPEC, v4, buf, sizeof buf) == NULL);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_TRUE(rt_net_ntop(AF_INET, NULL, buf, sizeof buf) == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);   /* Wine protocol.c test_addr_to_print */
    rt_thread_set_last_error(0);
    T_TRUE(rt_net_ntop(AF_INET, v4, NULL, 16) == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);   /* Wine protocol.c test_addr_to_print */

    T_CASE("IPv6, the forms of win-behaviour-check-5 Z7 (Wine test_addr_to_print, ntdll test_RtlIpv6AddressToString)");
    ntop6_is("1111:2222:3333:4444:5555:6666:0:8888", "1111:2222:3333:4444:5555:6666:0:8888");  /* a zero group on its own is 0 */
    ntop6_is("2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1");
    ntop6_is("::0.1.0.0", "::0.1.0.0");                                 /* first 96 bits zero, g6 not 0: dotted */
    ntop6_is("::13.1.68.3", "::13.1.68.3");
    ntop6_is("::204.152.189.116", "::204.152.189.116");
    ntop6_is("::ffff", "::ffff");                                       /* g6 0: hex */
    ntop6_is("::2", "::2");
    ntop6_is("::", "::");
    ntop6_is("::ffff:13.1.68.3", "::ffff:13.1.68.3");                   /* ::ffff:a.b.c.d only when g6 is not 0 */
    ntop6_is("::ffff:0.1.0.0", "::ffff:0.1.0.0");
    ntop6_is("::ffff:0:4403", "::ffff:0:4403");
    ntop6_is("::ffff:0:0", "::ffff:0:0");
    ntop6_is("::ffff:0:13.1.68.3", "::ffff:0:13.1.68.3");               /* ::ffff:0:a.b.c.d when g6 is not 0 */
    ntop6_is("1111:2222:3333:4444:0:5efe:129.144.52.38", "1111:2222:3333:4444:0:5efe:129.144.52.38");   /* ISATAP */
    ntop6_is("::200:5efe:129.144.52.38", "::200:5efe:129.144.52.38");
    ntop6_is("::5efe:0.0.0.0", "::5efe:0.0.0.0");
    ntop6_is("::100:5efe:8190:3426", "::100:5efe:8190:3426");           /* g4 0100: hex */
}

TEST(core, net_inet_addr)
{
    T_CASE("the four documented forms, in network order");
    LE_SET();
    T_EQ(rt_net_inet_addr("127.0.0.1"), 0x0100007fu);       /* bytes 7f 00 00 01 */
    T_EQ(rt_net_inet_addr("1.2.3.4"), 0x04030201u);
    T_EQ(rt_net_inet_addr("1.2.3"), 0x03000201u);           /* 1.2.0.3: the last part is 16 bits */
    T_EQ(rt_net_inet_addr("1.2"), 0x02000001u);             /* 1.0.0.2: 24 bits */
    T_EQ(rt_net_inet_addr("16909060"), 0x04030201u);        /* 0x01020304: 32 bits */
    T_EQ(rt_net_inet_addr("0.0.0.0"), 0u);
    T_EQ(rt_net_inet_addr("0x7f.0.0.1"), 0x0100007fu);      /* hex */
    T_EQ(rt_net_inet_addr("010.0.0.1"), 0x01000008u);       /* octal */
    T_EQ(rt_net_inet_addr("1.2.65535"), 0xffff0201u);

    T_CASE("what is not an address");
    const char *no[] = { "", "1.2.3.4.5", "256.1.1.1", "1.2.3.256", "1.2.65536", "1.16777216", "4294967296",
                         "a.b.c.d", "1..2", "1.2.3.", ".1.2.3", "08.1.1.1", "0x", "1.2.3.4x", "localhost",
                         " 3.4.5.6", "\t3.4.5.6", "  ", "\t" };   /* white space before the address: INADDR_NONE */
    for (unsigned i = 0; i < sizeof no / sizeof *no; i++) T_EQ(rt_net_inet_addr(no[i]), INADDR_NONE);
    T_EQ(rt_net_inet_addr("255.255.255.255"), INADDR_NONE);
    LE_UNTOUCHED();

    /* Windows (Wine ws2_32/tests/protocol.c, test_inet_addr; review Z8): NULL sets WSAEFAULT.
     * Before, NULL set nothing and a space stopped the text; both were wrong. */
    T_CASE("NULL gives INADDR_NONE and last error WSAEFAULT");
    rt_thread_set_last_error(0);
    T_EQ(rt_net_inet_addr(NULL), INADDR_NONE);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    /* Windows (the same test): one space gives 0; after a valid address the first white-space
     * character ends the text, so "1 2 3 4" is the one-part form "1"; success leaves the last
     * error alone. */
    T_CASE("one space gives 0; white space after a valid address ends the text");
    LE_SET();
    T_EQ(rt_net_inet_addr(" "), 0u);
    T_EQ(rt_net_inet_addr("3.4.5.6\t"), 0x06050403u);
    T_EQ(rt_net_inet_addr("3.4.5.6  7"), 0x06050403u);
    T_EQ(rt_net_inet_addr("1 2 3 4"), 0x01000000u);         /* bytes 00 00 00 01 */
    T_EQ(rt_net_inet_addr("1.2.3 .4"), 0x03000201u);        /* 1.2.0.3 */
    T_EQ(rt_net_inet_addr("1.2.3 \xfe\xff"), 0x03000201u);  /* 1.2.0.3 */
    LE_UNTOUCHED();

    /* The same test loops over every byte after "1.2.3": white space gives 1.2.0.3, any other
     * byte INADDR_NONE.  A digit is skipped: it goes on the number ("1.2.30" is 1.2.0.30). */
    T_CASE("after 1.2.3, each byte: white space gives 1.2.0.3, any other gives INADDR_NONE");
    for (int c = 1; c <= 255; c++) {
        if (c >= '0' && c <= '9') continue;
        char t[7] = "1.2.3";                                /* t[6] is the terminating 0 */
        t[5] = (char)c;
        bool white = c == ' ' || (c >= '\t' && c <= '\r');  /* space, \t \n \v \f \r */
        T_EQ(rt_net_inet_addr(t), white ? 0x03000201u : INADDR_NONE);
    }
    LE_UNTOUCHED();
}
