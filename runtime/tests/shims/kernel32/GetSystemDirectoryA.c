#include <string.h>

#include "rt/test.h"

/* RT_KNOWN_SYSTEM (rt/path.h): C:\Windows\system32, 19 bytes, no backslash at the end. */
#define LEN   19u
#define FILL  0xCDu      /* what a buffer holds before a call */

static const char text[] = "C:\\Windows\\system32";   /* 19 bytes and the terminator */

static void fill(char *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) b[i] = (char)FILL;
}

/* True when the bytes from `from` up to `to` (exclusive) still hold FILL. */
static int untouched(const char *b, unsigned from, unsigned to)
{
    for (unsigned i = from; i < to; i++)
        if ((unsigned char)b[i] != FILL) return 0;
    return 1;
}

TEST(kernel32, GetSystemDirectoryA)
{
    char got[MAX_PATH], small[MAX_PATH];
    UINT n;

    T_CASE("returns the length without the terminator and writes the text with its terminator");
    fill(got, MAX_PATH);
    n = CALL(kernel32, GetSystemDirectoryA, got, MAX_PATH);
    T_EQ(n, LEN);
    T_TRUE(memcmp(got, text, sizeof text) == 0);       /* the text and its terminator */
    T_TRUE(untouched(got, LEN + 1, MAX_PATH));         /* nothing written past the terminator */

    T_CASE("a buffer one unit too small returns the length plus one and keeps every unit");
    fill(small, MAX_PATH);
    T_EQ(CALL(kernel32, GetSystemDirectoryA, small, LEN), LEN + 1);
    T_TRUE(untouched(small, 0, MAX_PATH));

    T_CASE("a NULL buffer with size 0 returns the length plus one");
    T_EQ(CALL(kernel32, GetSystemDirectoryA, NULL, 0), LEN + 1);

    T_CASE("a buffer of exactly the length plus one holds the text");
    fill(small, MAX_PATH);
    T_EQ(CALL(kernel32, GetSystemDirectoryA, small, LEN + 1), LEN);
    T_TRUE(memcmp(small, text, sizeof text) == 0);
    T_TRUE(untouched(small, LEN + 1, MAX_PATH));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetSystemDirectoryA, got, MAX_PATH);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetSystemDirectoryA, NULL, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetSystemDirectoryA, small, LEN);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRAPS(CALL(kernel32, GetSystemDirectoryA, NULL, LEN + 1), "rt_path_known_a: no buffer");
}
