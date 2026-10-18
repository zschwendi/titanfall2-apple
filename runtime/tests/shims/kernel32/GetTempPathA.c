#include <string.h>

#include "rt/test.h"

/* What the guest's TMP holds at start-up, and so what GetTempPath answers (docs/RUNTIME.md
 * 7.1 and 8.2).  The test writes the text itself; the core is not the oracle here. */
#define TEXT  "C:\\Users\\Player\\AppData\\Local\\Temp\\"
#define LEN   35u           /* the text without its terminator */
#define FILL  0xCDu         /* what a buffer holds before a call */

/* True if the n bytes at b all hold v. */
static int all(const char *b, unsigned n, unsigned char v)
{
    for (unsigned i = 0; i < n; i++)
        if ((unsigned char)b[i] != v) return 0;
    return 1;
}

TEST(kernel32, GetTempPathA)
{
    char buf[MAX_PATH], small[MAX_PATH];
    DWORD n;

    T_CASE("returns the temporary directory with its trailing backslash, and the length without the terminator");
    memset(buf, FILL, sizeof buf);
    n = CALL(kernel32, GetTempPathA, MAX_PATH, buf);
    T_EQ(n, LEN);
    T_TRUE(memcmp(buf, TEXT, LEN + 1) == 0);            /* the text, its backslash and its terminator */
    T_TRUE(all(buf + LEN + 1, MAX_PATH - LEN - 1, FILL)); /* nothing written past the terminator */

    T_CASE("a buffer one byte too small returns the length plus one and keeps every byte");
    memset(small, FILL, sizeof small);
    T_EQ(CALL(kernel32, GetTempPathA, LEN, small), LEN + 1);
    T_TRUE(all(small, MAX_PATH, FILL));                 /* the buffer is untouched */

    T_CASE("a size query, a size of 0 with a NULL buffer, returns the length plus one");
    T_EQ(CALL(kernel32, GetTempPathA, 0, NULL), LEN + 1);

    T_CASE("a buffer of exactly the length plus one holds the text");
    memset(small, FILL, sizeof small);
    n = CALL(kernel32, GetTempPathA, LEN + 1, small);
    T_EQ(n, LEN);
    T_TRUE(memcmp(small, TEXT, LEN + 1) == 0);
    T_TRUE(all(small + LEN + 1, MAX_PATH - LEN - 1, FILL));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetTempPathA, MAX_PATH, buf);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetTempPathA, LEN, small);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, GetTempPathA, 0, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRAPS(CALL(kernel32, GetTempPathA, LEN + 1, NULL), "no buffer");
}
