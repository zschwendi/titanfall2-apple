#include <string.h>

#include "rt/test.h"
#include "rt/win/net.h"

/* The oracle is the value the core's comment and the documentation give for each text.  An
 * address is in network order: 127.0.0.1 is the bytes 7F 00 00 01 in memory, which a
 * little-endian host reads as 0x0100007F. */
TEST(wsock32, inet_addr)
{
    ULONG v;
    unsigned char b[4];

    T_CASE("dotted quad gives the network-order address");
    v = CALL(wsock32, inet_addr, "127.0.0.1");
    T_EQ(v, 0x0100007Fu);
    memcpy(b, &v, sizeof b);
    T_TRUE(memcmp(b, "\x7f\x00\x00\x01", 4) == 0);

    T_CASE("three-part, two-part and one-part forms");
    T_EQ(CALL(wsock32, inet_addr, "1.2.3"), 0x03000201u);
    T_EQ(CALL(wsock32, inet_addr, "1.2"), 0x02000001u);
    T_EQ(CALL(wsock32, inet_addr, "2130706433"), 0x0100007Fu);

    T_CASE("hex and octal parts");
    T_EQ(CALL(wsock32, inet_addr, "0x7f.1"), 0x0100007Fu);
    T_EQ(CALL(wsock32, inet_addr, "0177.0.0.01"), 0x0100007Fu);

    /* Windows (Wine ws2_32/tests/protocol.c, test_inet_addr; review Z8): NULL sets WSAEFAULT.
     * This test asserted the old behaviour (NULL left last error alone). */
    T_CASE("NULL gives INADDR_NONE and WSAEFAULT");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(wsock32, inet_addr, NULL), INADDR_NONE);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_CASE("empty string gives INADDR_NONE");
    T_EQ(CALL(wsock32, inet_addr, ""), INADDR_NONE);

    T_CASE("text that is not an address gives INADDR_NONE");
    T_EQ(CALL(wsock32, inet_addr, "abc"), INADDR_NONE);
    T_EQ(CALL(wsock32, inet_addr, " 3.4.5.6"), INADDR_NONE);     /* white space before: INADDR_NONE */
    T_EQ(CALL(wsock32, inet_addr, "\t3.4.5.6"), INADDR_NONE);

    T_CASE("five parts give INADDR_NONE");
    T_EQ(CALL(wsock32, inet_addr, "1.2.3.4.5"), INADDR_NONE);

    T_CASE("a part above 255 gives INADDR_NONE");
    T_EQ(CALL(wsock32, inet_addr, "1.2.3.256"), INADDR_NONE);

    T_CASE("255.255.255.255 gives INADDR_NONE");
    T_EQ(CALL(wsock32, inet_addr, "255.255.255.255"), INADDR_NONE);

    /* Windows (the same test; review Z8): a single space gives zero. */
    T_CASE("one space gives 0");
    T_EQ(CALL(wsock32, inet_addr, " "), 0u);

    /* Windows (the same test): after a valid address the first white-space character ends the
     * text and what follows is ignored: "1 2 3 4" is the one-part form "1", 0.0.0.1. */
    T_CASE("white space after a valid address ends the text");
    T_EQ(CALL(wsock32, inet_addr, "3.4.5.6\t"), 0x06050403u);
    T_EQ(CALL(wsock32, inet_addr, "127.0.0.1 junk"), 0x0100007Fu);
    T_EQ(CALL(wsock32, inet_addr, "1 2 3 4"), 0x01000000u);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(wsock32, inet_addr, "127.0.0.1");
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(wsock32, inet_addr, "");
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(wsock32, inet_addr, " ");
    T_EQ(T_LAST_ERROR(), 0x1234);
}
