#include "rt/test.h"
#include "rt/file.h"
#include "rt/log.h"

/* The runtime has no console, so every call fails as Windows fails for a handle that is not a
 * console's. The function has no pointer argument, so nothing is written. */
TEST(kernel32, SetConsoleMode)
{
    HANDLE std_in;
    HANDLE hs[3];
    BOOL r;
    int i;

    std_in = rt_std_get(STD_INPUT_HANDLE);
    hs[0] = NULL;
    hs[1] = (HANDLE)0x44;
    hs[2] = std_in;

    T_CASE("fails for a NULL handle");
    r = CALL(kernel32, SetConsoleMode, NULL, 0x7);
    T_EQ(r, FALSE);

    T_CASE("fails for a handle that is not a console");
    r = CALL(kernel32, SetConsoleMode, (HANDLE)0x44, 0x7);
    T_EQ(r, FALSE);

    T_CASE("fails for the standard input handle");
    r = CALL(kernel32, SetConsoleMode, std_in, 0x7);
    T_EQ(r, FALSE);

    T_CASE("sets ERROR_INVALID_HANDLE");
    for (i = 0; i < 3; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        r = CALL(kernel32, SetConsoleMode, hs[i], 0x7);
        T_EQ(r, FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }

    T_CASE("logs that there is no console");
    r = CALL(kernel32, SetConsoleMode, NULL, 0x7);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "SetConsoleMode: there is no console");
}
