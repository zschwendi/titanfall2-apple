#include "rt/test.h"
#include "rt/file.h"
#include "rt/log.h"

/* The runtime has no console, so every call fails as Windows fails for a handle that is not a
 * console's, and writes nothing through lpcNumberOfEvents. */
TEST(kernel32, GetNumberOfConsoleInputEvents)
{
    HANDLE std_in;
    HANDLE hs[3];
    DWORD count;
    BOOL r;
    int i;

    std_in = rt_std_get(STD_INPUT_HANDLE);
    hs[0] = NULL;
    hs[1] = (HANDLE)0x44;
    hs[2] = std_in;

    T_CASE("fails for a NULL handle");
    count = 0xA5A5A5A5;
    r = CALL(kernel32, GetNumberOfConsoleInputEvents, NULL, &count);
    T_EQ(r, FALSE);

    T_CASE("fails for a handle that is not a console");
    count = 0xA5A5A5A5;
    r = CALL(kernel32, GetNumberOfConsoleInputEvents, (HANDLE)0x44, &count);
    T_EQ(r, FALSE);

    T_CASE("fails for the standard input handle");
    count = 0xA5A5A5A5;
    r = CALL(kernel32, GetNumberOfConsoleInputEvents, std_in, &count);
    T_EQ(r, FALSE);

    T_CASE("fails with a NULL lpcNumberOfEvents");
    for (i = 0; i < 3; i++) {
        r = CALL(kernel32, GetNumberOfConsoleInputEvents, hs[i], NULL);
        T_EQ(r, FALSE);
    }

    T_CASE("leaves a preset count alone");
    for (i = 0; i < 3; i++) {
        count = 0xA5A5A5A5;
        r = CALL(kernel32, GetNumberOfConsoleInputEvents, hs[i], &count);
        T_EQ(r, FALSE);
        T_EQ(count, 0xA5A5A5A5);
    }

    T_CASE("sets ERROR_INVALID_HANDLE");
    for (i = 0; i < 3; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        count = 0xA5A5A5A5;
        r = CALL(kernel32, GetNumberOfConsoleInputEvents, hs[i], &count);
        T_EQ(r, FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }

    T_CASE("logs that there is no console");
    count = 0xA5A5A5A5;
    r = CALL(kernel32, GetNumberOfConsoleInputEvents, NULL, &count);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "GetNumberOfConsoleInputEvents: there is no console");
}
