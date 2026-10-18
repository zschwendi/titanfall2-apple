#include <string.h>

#include "rt/test.h"

/* RT_KNOWN_SYSTEM (rt/path.h): C:\Windows\system32, 19 WCHARs, no backslash at the end. */
#define LEN   19u
#define FILL  0xCDCDu   /* what a buffer holds before a call */

static const char text[] = "C:\\Windows\\system32";   /* ASCII: each WCHAR is one of these bytes */

static void fill(WCHAR *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) b[i] = (WCHAR)FILL;
}

/* True when the WCHARs from `from` up to `to` (exclusive) still hold FILL. */
static int untouched(const WCHAR *b, unsigned from, unsigned to)
{
    for (unsigned i = from; i < to; i++)
        if (b[i] != FILL) return 0;
    return 1;
}

/* True when the first sizeof text WCHARs from b are the text and its terminator. */
static int holds_text(const WCHAR *b)
{
    for (unsigned i = 0; i < sizeof text; i++)
        if (b[i] != (WCHAR)(unsigned char)text[i]) return 0;
    return 1;
}

TEST(kernel32, GetSystemDirectoryW)
{
    WCHAR got[MAX_PATH], small[MAX_PATH];
    UINT n;

    T_CASE("returns the length without the terminator and writes the text with its terminator");
    fill(got, MAX_PATH);
    n = CALL(kernel32, GetSystemDirectoryW, got, MAX_PATH);
    T_EQ(n, LEN);
    T_TRUE(holds_text(got));                           /* the text and its terminator */
    T_TRUE(untouched(got, LEN + 1, MAX_PATH));         /* nothing written past the terminator */

    T_CASE("a buffer one unit too small returns the length plus one and keeps every unit");
    fill(small, MAX_PATH);
    T_EQ(CALL(kernel32, GetSystemDirectoryW, small, LEN), LEN + 1);
    T_TRUE(untouched(small, 0, MAX_PATH));

    T_CASE("a NULL buffer with size 0 returns the length plus one");
    T_EQ(CALL(kernel32, GetSystemDirectoryW, NULL, 0), LEN + 1);

    T_CASE("a buffer of exactly the length plus one holds the text");
    fill(small, MAX_PATH);
    T_EQ(CALL(kernel32, GetSystemDirectoryW, small, LEN + 1), LEN);
    T_TRUE(holds_text(small));
    T_TRUE(untouched(small, LEN + 1, MAX_PATH));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetSystemDirectoryW, got, MAX_PATH);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetSystemDirectoryW, NULL, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetSystemDirectoryW, small, LEN);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRAPS(CALL(kernel32, GetSystemDirectoryW, NULL, LEN + 1), "rt_path_known_w: no buffer");
}
