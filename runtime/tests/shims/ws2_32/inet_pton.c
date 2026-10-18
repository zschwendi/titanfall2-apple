#include "rt/test.h"
#include "rt/win/net.h"

/* Every call starts with the buffer filled with PTON_FILL.  A call that stores writes bytes
 * that differ from it; the checks count bytes, so one wrong byte fails one check. */
#define PTON_FILL 0xEE
#define PTON_LEN  24

static void fill(unsigned char *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) p[i] = PTON_FILL;
}

/* How many of the n bytes at p are not the fill byte. */
static unsigned changed(const unsigned char *p, unsigned n)
{
    unsigned c = 0;
    for (unsigned i = 0; i < n; i++) if (p[i] != PTON_FILL) c++;
    return c;
}

/* How many of the n bytes at p differ from the n bytes at want. */
static unsigned differ(const unsigned char *p, const unsigned char *want, unsigned n)
{
    unsigned c = 0;
    for (unsigned i = 0; i < n; i++) if (p[i] != want[i]) c++;
    return c;
}

static const unsigned char v4_loopback[4] = { 0x7F, 0x00, 0x00, 0x01 };
static const unsigned char v6_mapped_loopback[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x01
};
static const unsigned char v6_loopback[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01
};
/* "1::001.2.3.4" and "1::1.002.3.4": the dotted tail's leading zeros are read as decimal
 * (Wine ws2_32/tests/protocol.c, test_inet_pton, the IPv6 table; win-behaviour-check-5 Z6). */
static const unsigned char v6_1_1234[16] = {
    0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02, 0x03, 0x04
};

TEST(ws2_32, inet_pton)
{
    unsigned char buf[PTON_LEN];

    T_CASE("AF_INET dotted-decimal text gives 1 and four bytes in network order");
    fill(buf, PTON_LEN);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, "127.0.0.1", buf), 1);
    T_EQ(differ(buf, v4_loopback, 4), 0);
    T_EQ(changed(buf + 4, PTON_LEN - 4), 0);

    T_CASE("AF_INET6 text gives 1 and sixteen bytes in network order");
    fill(buf, PTON_LEN);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "::FFFF:127.0.0.1", buf), 1);
    T_EQ(differ(buf, v6_mapped_loopback, 16), 0);
    T_EQ(changed(buf + 16, PTON_LEN - 16), 0);
    fill(buf, PTON_LEN);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "::1", buf), 1);
    T_EQ(differ(buf, v6_loopback, 16), 0);
    T_EQ(changed(buf + 16, PTON_LEN - 16), 0);
    fill(buf, PTON_LEN);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "1::001.2.3.4", buf), 1);
    T_EQ(differ(buf, v6_1_1234, 16), 0);
    T_EQ(changed(buf + 16, PTON_LEN - 16), 0);
    fill(buf, PTON_LEN);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "1::1.002.3.4", buf), 1);
    T_EQ(differ(buf, v6_1_1234, 16), 0);
    T_EQ(changed(buf + 16, PTON_LEN - 16), 0);

    T_CASE("text that is not an address of the family gives 0");
    fill(buf, PTON_LEN);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, "hello", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, "256.0.0.1", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "hello", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "[::1]", buf), 0);
    /* a leading zero in the IPv4 text, and a scope (a '%') in either family */
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, "1.1.1.010", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, "1.1.1.00", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, "01.2.3.4", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "1::0001.2.3.4", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "::%16", buf), 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "fe80::1%1", buf), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("another family gives -1 and WSAEAFNOSUPPORT");
    fill(buf, PTON_LEN);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(ws2_32, inet_pton, 0, "127.0.0.1", buf), -1);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(ws2_32, inet_pton, 99, "::1", buf), -1);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_EQ(changed(buf, PTON_LEN), 0);

    T_CASE("success leaves last error alone");
    fill(buf, PTON_LEN);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, "127.0.0.1", buf), 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "::1", buf), 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "1::001.2.3.4", buf), 1);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* A NULL text is looked at before the family: a bad family does not change the answer. */
    T_CASE("NULL text or buffer gives -1 and WSAEFAULT");
    fill(buf, PTON_LEN);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET, NULL, buf), -1);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_UNSPEC, NULL, buf), -1);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(ws2_32, inet_pton, AF_INET6, "::1", NULL), -1);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_EQ(changed(buf, PTON_LEN), 0);
}
