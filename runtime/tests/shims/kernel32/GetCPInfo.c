#include <string.h>

#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/win/nls.h"

/* A single-byte page as the core fills it: MaxCharSize 1, DefaultChar '?' then 0, LeadByte all
 * zero.  Each field is checked whole, so every byte it occupies is checked.  The two bytes of
 * padding after LeadByte are not a field of CPINFO and are not checked. */
static void check_single_byte(const CPINFO *c)
{
    unsigned i, bad = 0;

    T_EQ(c->MaxCharSize, 1);
    T_EQ(c->DefaultChar[0], '?');
    T_EQ(c->DefaultChar[1], 0);
    for (i = 0; i < MAX_LEADBYTES; i++)
        if (c->LeadByte[i] != 0) bad++;
    T_EQ(bad, 0);
}

/* The number of bytes in p[0..n) that are not `fill`. */
static unsigned count_not(const void *p, size_t n, unsigned char fill)
{
    const unsigned char *b = p;
    unsigned i, bad = 0;

    for (i = 0; i < n; i++)
        if (b[i] != fill) bad++;
    return bad;
}

TEST(kernel32, GetCPInfo)
{
    CPINFO info;

    T_CASE("1252 and CP_ACP fill the CPINFO fields");
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, 1252, &info), TRUE);
    check_single_byte(&info);
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, CP_ACP, &info), TRUE);
    check_single_byte(&info);

    T_CASE("437 and CP_OEMCP fill the CPINFO fields");
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, 437, &info), TRUE);
    check_single_byte(&info);
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, CP_OEMCP, &info), TRUE);
    check_single_byte(&info);

    T_CASE("CP_THREAD_ACP fills the CPINFO fields as 1252");
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, CP_THREAD_ACP, &info), TRUE);
    check_single_byte(&info);

    T_CASE("20127 and 28591 fill the CPINFO fields");
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, 20127, &info), TRUE);
    check_single_byte(&info);
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, 28591, &info), TRUE);
    check_single_byte(&info);

    T_CASE("NULL fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, GetCPInfo, 1252, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, GetCPInfo, CP_OEMCP, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a code page the machine lacks fails with ERROR_INVALID_PARAMETER and writes nothing");
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, 932, &info), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(count_not(&info, sizeof info, 0xEE), 0);
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, CP_MACCP, &info), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(count_not(&info, sizeof info, 0xEE), 0);
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, CP_UTF7, &info), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(count_not(&info, sizeof info, 0xEE), 0);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetCPInfo, 437, &info), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("CP_UTF8 returns TRUE and MaxCharSize 4");
    memset(&info, 0xEE, sizeof info);
    T_EQ(CALL(kernel32, GetCPInfo, CP_UTF8, &info), TRUE);
    T_EQ(info.MaxCharSize, 4);
}
