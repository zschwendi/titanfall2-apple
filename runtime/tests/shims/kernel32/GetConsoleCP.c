#include "rt/test.h"
#include "rt/log.h"

/* The runtime has no console, so there is no console input code page: the call returns 0. */
TEST(kernel32, GetConsoleCP)
{
    UINT cp;

    T_CASE("returns 0 when the process has no console");
    cp = CALL(kernel32, GetConsoleCP);
    T_EQ(cp, 0);

    T_CASE("sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    cp = CALL(kernel32, GetConsoleCP);
    T_EQ(cp, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no console");
    cp = CALL(kernel32, GetConsoleCP);
    T_EQ(cp, 0);
    T_LOGGED(RT_CH_FILE, "GetConsoleCP: there is no console");
}
