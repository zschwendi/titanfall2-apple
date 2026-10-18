#include <string.h>
#include "rt/test.h"
#include "rt/log.h"

/* The runtime has no console, so every call fails and writes through neither pointer. */
TEST(kernel32, PeekConsoleInputA)
{
    unsigned char buf[40];
    unsigned char pattern[40];
    DWORD count;
    BOOL r;

    memset(pattern, 0xA5, sizeof pattern);

    T_CASE("fails for a handle that is not a console");
    memset(buf, 0xA5, sizeof buf);
    count = 0xA5A5A5A5;
    r = CALL(kernel32, PeekConsoleInputA, (HANDLE)0x44, buf, sizeof buf, &count);
    T_EQ(r, FALSE);

    T_CASE("fails with every pointer NULL");
    r = CALL(kernel32, PeekConsoleInputA, (HANDLE)0x44, NULL, sizeof buf, NULL);
    T_EQ(r, FALSE);

    T_CASE("leaves a preset buffer and count alone");
    memset(buf, 0xA5, sizeof buf);
    count = 0xA5A5A5A5;
    r = CALL(kernel32, PeekConsoleInputA, (HANDLE)0x44, buf, sizeof buf, &count);
    T_EQ(r, FALSE);
    T_TRUE(memcmp(buf, pattern, sizeof buf) == 0);
    T_EQ(count, 0xA5A5A5A5);

    T_CASE("sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, PeekConsoleInputA, (HANDLE)0x44, buf, sizeof buf, &count);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no console");
    r = CALL(kernel32, PeekConsoleInputA, (HANDLE)0x44, buf, sizeof buf, &count);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "PeekConsoleInputA: there is no console");
}
