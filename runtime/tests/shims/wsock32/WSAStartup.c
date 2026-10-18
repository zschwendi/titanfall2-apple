#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/win/net.h"

/* True if the bytes [from, to) of p are all v. */
static int filled_with(const void *p, size_t from, size_t to, unsigned char v)
{
    const unsigned char *b = p;
    for (size_t i = from; i < to; i++)
        if (b[i] != v) return 0;
    return 1;
}

#define PRESET_VENDOR  ((char *)(uintptr_t)0xdeadbeef)

/* Every byte 0xAA, lpVendorInfo preset: what WSAStartup must leave of lpVendorInfo. */
static void fresh(WSADATA *d)
{
    memset(d, 0xAA, sizeof *d);
    d->lpVendorInfo = PRESET_VENDOR;
}

/* The fields WSAStartup writes (Wine ws2_32/tests/protocol.c, test_startup; WSAStartup's page;
 * review Z5): the version, the high version 2.2, both texts with zeros to the end of each array,
 * and the limits 32767 and 65467 for a major version 1, 0 otherwise.  lpVendorInfo is not
 * written, so it still holds the preset. */
static void check_written(const WSADATA *d, WORD version, unsigned max_sockets, unsigned max_udp)
{
    T_EQ(d->wVersion, version);
    T_EQ(d->wHighVersion, 0x0202);
    T_TRUE(strcmp(d->szDescription, "WinSock 2.0") == 0);
    T_TRUE(filled_with(d->szDescription, sizeof "WinSock 2.0", sizeof d->szDescription, 0));
    T_TRUE(strcmp(d->szSystemStatus, "Running") == 0);
    T_TRUE(filled_with(d->szSystemStatus, sizeof "Running", sizeof d->szSystemStatus, 0));
    T_EQ(d->iMaxSockets, max_sockets);
    T_EQ(d->iMaxUdpDg, max_udp);
    T_TRUE(d->lpVendorInfo == PRESET_VENDOR);
}

TEST(wsock32, WSAStartup)
{
    WSADATA d;

    T_CASE("requested 2.2 succeeds and fills the structure except lpVendorInfo");
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0202, &d), 0);
    check_written(&d, 0x0202, 0, 0);
    T_EQ(rt_net_cleanup(), 0);

    T_CASE("requested 1.1 keeps wVersion 1.1, reports high version 2.2 and the 1.x limits");
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0101, &d), 0);
    check_written(&d, 0x0101, 32767, 65467);
    T_EQ(rt_net_cleanup(), 0);

    T_CASE("requested 1.2 to 1.255 gives wVersion 1.1");
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0201, &d), 0);         /* 1.2: Test line of review Z5 */
    check_written(&d, 0x0101, 32767, 65467);
    T_EQ(rt_net_cleanup(), 0);
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0xFF01, &d), 0);         /* 1.255 */
    check_written(&d, 0x0101, 32767, 65467);
    T_EQ(rt_net_cleanup(), 0);

    T_CASE("a version above 2.2 gives wVersion 2.2");
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0302, &d), 0);         /* 2.3 */
    check_written(&d, 0x0202, 0, 0);
    T_EQ(rt_net_cleanup(), 0);
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0003, &d), 0);         /* 3.0 */
    check_written(&d, 0x0202, 0, 0);
    T_EQ(rt_net_cleanup(), 0);

    T_CASE("NULL output pointer gives WSAEFAULT");
    T_EQ(CALL(wsock32, WSAStartup, 0x0202, NULL), WSAEFAULT);

    T_CASE("major version 0 gives WSAVERNOTSUPPORTED and still fills the structure");
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0100, &d), WSAVERNOTSUPPORTED);   /* 0.1: Test line of review Z5 */
    check_written(&d, 0x0202, 0, 0);
    T_EQ(CALL(wsock32, WSACleanup), SOCKET_ERROR);          /* the failed start was not counted */

    T_CASE("last error is left alone on every return");
    CALL(kernel32, SetLastError, 0xDEAD);
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0202, &d), 0);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_EQ(rt_net_cleanup(), 0);
    CALL(kernel32, SetLastError, 0xDEAD);
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0201, &d), 0);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_EQ(rt_net_cleanup(), 0);
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(wsock32, WSAStartup, 0x0202, NULL), WSAEFAULT);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    CALL(kernel32, SetLastError, 0xDEAD);
    fresh(&d);
    T_EQ(CALL(wsock32, WSAStartup, 0x0100, &d), WSAVERNOTSUPPORTED);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
}
