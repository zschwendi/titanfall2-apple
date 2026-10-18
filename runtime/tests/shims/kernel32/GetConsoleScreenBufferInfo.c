#include <string.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/log.h"
#include "rt/win/file.h"

/* The runtime has no console, so every call fails and writes nothing through the structure. */
TEST(kernel32, GetConsoleScreenBufferInfo)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    const unsigned char *p = (const unsigned char *)&csbi;
    HANDLE std_out;
    BOOL r;
    int i, bad;

    T_CASE("fails for a NULL handle");
    memset(&csbi, 0, sizeof csbi);
    r = CALL(kernel32, GetConsoleScreenBufferInfo, NULL, &csbi);
    T_EQ(r, FALSE);

    T_CASE("fails for a handle that is not a console");
    r = CALL(kernel32, GetConsoleScreenBufferInfo, (HANDLE)0x44, &csbi);
    T_EQ(r, FALSE);

    T_CASE("fails for the standard output handle");
    std_out = rt_std_get(STD_OUTPUT_HANDLE);
    r = CALL(kernel32, GetConsoleScreenBufferInfo, std_out, &csbi);
    T_EQ(r, FALSE);

    T_CASE("fails with a NULL lpConsoleScreenBufferInfo");
    r = CALL(kernel32, GetConsoleScreenBufferInfo, (HANDLE)0x44, NULL);
    T_EQ(r, FALSE);

    T_CASE("leaves a preset structure alone");
    memset(&csbi, 0xA5, sizeof csbi);
    r = CALL(kernel32, GetConsoleScreenBufferInfo, (HANDLE)0x44, &csbi);
    T_EQ(r, FALSE);
    bad = 0;
    for (i = 0; i < (int)sizeof csbi; i++)
        if (p[i] != 0xA5) bad++;
    T_EQ(bad, 0);

    T_CASE("sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, GetConsoleScreenBufferInfo, NULL, &csbi);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no console");
    r = CALL(kernel32, GetConsoleScreenBufferInfo, (HANDLE)0x44, &csbi);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "GetConsoleScreenBufferInfo: there is no console");
}
