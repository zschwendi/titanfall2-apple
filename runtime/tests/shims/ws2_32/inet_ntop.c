#include <string.h>

#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/win/net.h"

/* The buffer is filled with 'Z' before each call, so every byte the call writes shows, and a byte it
 * must leave alone is seen.  `total` is the size of the buffer. */

/* 1 if buf holds `want` and its terminator, and every byte after that up to total is still 'Z'. */
static int holds(const char *buf, size_t total, const char *want)
{
    size_t i, len = strlen(want);

    if (len + 1 > total) return 0;
    for (i = 0; i < len; i++)
        if (buf[i] != want[i]) return 0;
    if (buf[len] != 0) return 0;
    for (i = len + 1; i < total; i++)
        if (buf[i] != 'Z') return 0;
    return 1;
}

/* 1 if the first n bytes of buf are still the preset 'Z'. */
static int untouched(const char *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (buf[i] != 'Z') return 0;
    return 1;
}

TEST(ws2_32, inet_ntop)
{
    /* The addresses in network byte order, 16 bytes for AF_INET6 written as eight 16-bit groups. */
    static const unsigned char v4[4] = { 0xC0, 0xA8, 0x00, 0x01 };
    static const unsigned char loop[16] = {
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x01 };        /* ::1 */
    static const unsigned char db8[16] = {
        0x20, 0x01,  0x0D, 0xB8,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x01 };        /* 2001:0db8:0:0:0:0:0:1 */
    static const unsigned char run1[16] = {
        0x20, 0x01,  0x0D, 0xB8,  0x00, 0x00,  0x00, 0x01,
        0x00, 0x01,  0x00, 0x01,  0x00, 0x01,  0x00, 0x01 };        /* 2001:db8:0:1:1:1:1:1 */
    static const unsigned char stop2[16] = {
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x02 };        /* ::2 */
    /* The forms of win-behaviour-check-5 Z7 (Wine ws2_32 test_addr_to_print; ntdll test_RtlIpv6AddressToString). */
    static const unsigned char single0[16] = {                  /* 1111:2222:3333:4444:5555:6666:0:8888 */
        0x11, 0x11,  0x22, 0x22,  0x33, 0x33,  0x44, 0x44,
        0x55, 0x55,  0x66, 0x66,  0x00, 0x00,  0x88, 0x88 };
    static const unsigned char all0[16] = { 0 };                /* :: */
    static const unsigned char compat1[16] = {                  /* ::0.1.0.0 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x00, 0x00,  0x00, 0x01,  0x00, 0x00 };
    static const unsigned char compat2[16] = {                  /* ::13.1.68.3 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x00, 0x00,  0x0D, 0x01,  0x44, 0x03 };
    static const unsigned char compat3[16] = {                  /* ::204.152.189.116 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x00, 0x00,  0xCC, 0x98,  0xBD, 0x74 };
    static const unsigned char ffff0[16] = {                    /* ::ffff (0.0.255.255) */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0xFF, 0xFF };
    static const unsigned char ffff13[16] = {                   /* ::ffff:13.1.68.3 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0xFF, 0xFF,  0x0D, 0x01,  0x44, 0x03 };
    static const unsigned char ffff01[16] = {                   /* ::ffff:0.1.0.0 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0xFF, 0xFF,  0x00, 0x01,  0x00, 0x00 };
    static const unsigned char ffff4403[16] = {                 /* ::ffff:0:4403 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0xFF, 0xFF,  0x00, 0x00,  0x44, 0x03 };
    static const unsigned char ffff00[16] = {                   /* ::ffff:0:0 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0xFF, 0xFF,  0x00, 0x00,  0x00, 0x00 };
    static const unsigned char ffff4_13[16] = {                 /* ::ffff:0:13.1.68.3 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0xFF, 0xFF,  0x00, 0x00,  0x0D, 0x01,  0x44, 0x03 };
    static const unsigned char isatap1[16] = {                  /* 1111:2222:3333:4444:0:5efe:129.144.52.38 */
        0x11, 0x11,  0x22, 0x22,  0x33, 0x33,  0x44, 0x44,
        0x00, 0x00,  0x5E, 0xFE,  0x81, 0x90,  0x34, 0x26 };
    static const unsigned char isatap2[16] = {                  /* ::200:5efe:129.144.52.38 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x02, 0x00,  0x5E, 0xFE,  0x81, 0x90,  0x34, 0x26 };
    static const unsigned char isatap3[16] = {                  /* ::5efe:0.0.0.0 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x00,  0x5E, 0xFE,  0x00, 0x00,  0x00, 0x00 };
    static const unsigned char isatap4[16] = {                  /* ::100:5efe:8190:3426 */
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,
        0x01, 0x00,  0x5E, 0xFE,  0x81, 0x90,  0x34, 0x26 };
    typedef struct { const unsigned char *a; const char *want; } v6case;
    static const v6case single[] = {
        { single0, "1111:2222:3333:4444:5555:6666:0:8888" }, { run1, "2001:db8:0:1:1:1:1:1" } };
    static const v6case zero96[] = {
        { compat1, "::0.1.0.0" }, { compat2, "::13.1.68.3" }, { compat3, "::204.152.189.116" },
        { ffff0, "::ffff" }, { stop2, "::2" }, { all0, "::" } };
    static const v6case ffff[] = {
        { ffff13, "::ffff:13.1.68.3" }, { ffff01, "::ffff:0.1.0.0" }, { ffff4403, "::ffff:0:4403" },
        { ffff00, "::ffff:0:0" } };
    static const v6case isatap[] = {
        { ffff4_13, "::ffff:0:13.1.68.3" }, { isatap1, "1111:2222:3333:4444:0:5efe:129.144.52.38" },
        { isatap2, "::200:5efe:129.144.52.38" }, { isatap3, "::5efe:0.0.0.0" }, { isatap4, "::100:5efe:8190:3426" } };
    char buf[64];
    const char *r;
    size_t i;

    T_CASE("AF_INET bytes C0 A8 00 01 give 192.168.0.1 and the buffer is returned");
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET, v4, buf, 12);
    T_TRUE(r == buf);
    T_TRUE(holds(buf, sizeof buf, "192.168.0.1"));

    T_CASE("AF_INET6 ::1 gives ::1 and 2001:0db8:0:0:0:0:0:1 gives 2001:db8::1");
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET6, loop, buf, sizeof buf);
    T_TRUE(r == buf);
    T_TRUE(holds(buf, sizeof buf, "::1"));
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET6, db8, buf, 12);
    T_TRUE(r == buf);
    T_TRUE(holds(buf, sizeof buf, "2001:db8::1"));

    T_CASE("a buffer one byte too small gives NULL, STATUS_INVALID_PARAMETER and an untouched buffer");
    CALL(kernel32, SetLastError, 0x1234);
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET, v4, buf, 11);
    T_TRUE(r == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);
    T_TRUE(untouched(buf, sizeof buf));
    CALL(kernel32, SetLastError, 0x1234);
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET6, db8, buf, 11);
    T_TRUE(r == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);
    T_TRUE(untouched(buf, sizeof buf));

    T_CASE("a zero size or a NULL buffer gives NULL and STATUS_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET, v4, buf, 0);
    T_TRUE(r == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);
    T_TRUE(untouched(buf, sizeof buf));
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(ws2_32, inet_ntop, AF_INET, v4, NULL, sizeof buf);
    T_TRUE(r == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);

    T_CASE("a NULL address gives NULL and STATUS_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET, NULL, buf, sizeof buf);
    T_TRUE(r == NULL);
    T_EQ(T_LAST_ERROR(), (DWORD)STATUS_INVALID_PARAMETER);
    T_TRUE(untouched(buf, sizeof buf));

    T_CASE("family 0 and family 99 give NULL and WSAEAFNOSUPPORT");
    CALL(kernel32, SetLastError, 0x1234);
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, 0, v4, buf, sizeof buf);
    T_TRUE(r == NULL);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_TRUE(untouched(buf, sizeof buf));
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(ws2_32, inet_ntop, 99, v4, buf, sizeof buf);
    T_TRUE(r == NULL);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_TRUE(untouched(buf, sizeof buf));

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    memset(buf, 'Z', sizeof buf);
    r = CALL(ws2_32, inet_ntop, AF_INET, v4, buf, sizeof buf);
    T_TRUE(r == buf);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(ws2_32, inet_ntop, AF_INET6, loop, buf, sizeof buf);
    T_TRUE(r == buf);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("AF_INET6 a zero group on its own is written 0, not :: (1111:2222:3333:4444:5555:6666:0:8888 and 2001:db8:0:1:1:1:1:1)");
    for (i = 0; i < sizeof single / sizeof single[0]; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        memset(buf, 'Z', sizeof buf);
        r = CALL(ws2_32, inet_ntop, AF_INET6, single[i].a, buf, 64);
        T_TRUE(r == buf);
        T_TRUE(holds(buf, sizeof buf, single[i].want));
        T_EQ(T_LAST_ERROR(), 0x1234);
    }

    T_CASE("AF_INET6 first 96 bits zero: dotted when group 6 is not 0 (::0.1.0.0, ::13.1.68.3, ::204.152.189.116), hex when it is 0 (::ffff, ::2, ::)");
    for (i = 0; i < sizeof zero96 / sizeof zero96[0]; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        memset(buf, 'Z', sizeof buf);
        r = CALL(ws2_32, inet_ntop, AF_INET6, zero96[i].a, buf, 64);
        T_TRUE(r == buf);
        T_TRUE(holds(buf, sizeof buf, zero96[i].want));
        T_EQ(T_LAST_ERROR(), 0x1234);
    }

    T_CASE("AF_INET6 ::ffff:a.b.c.d is dotted only when group 6 is not 0 (::ffff:13.1.68.3, ::ffff:0.1.0.0), hex otherwise (::ffff:0:4403, ::ffff:0:0)");
    for (i = 0; i < sizeof ffff / sizeof ffff[0]; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        memset(buf, 'Z', sizeof buf);
        r = CALL(ws2_32, inet_ntop, AF_INET6, ffff[i].a, buf, 64);
        T_TRUE(r == buf);
        T_TRUE(holds(buf, sizeof buf, ffff[i].want));
        T_EQ(T_LAST_ERROR(), 0x1234);
    }

    T_CASE("AF_INET6 ::ffff:0:a.b.c.d is dotted when group 6 is not 0 (::ffff:0:13.1.68.3), and ISATAP addresses (group 5 5efe, group 4 0 or 0200) are dotted, group 4 0100 is hex (::100:5efe:8190:3426)");
    for (i = 0; i < sizeof isatap / sizeof isatap[0]; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        memset(buf, 'Z', sizeof buf);
        r = CALL(ws2_32, inet_ntop, AF_INET6, isatap[i].a, buf, 64);
        T_TRUE(r == buf);
        T_TRUE(holds(buf, sizeof buf, isatap[i].want));
        T_EQ(T_LAST_ERROR(), 0x1234);
    }
}
