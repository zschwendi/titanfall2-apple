#include "rt/test.h"
#include "rt/log.h"

TEST(kernel32, AllocConsole)
{
    BOOL r;

    T_CASE("returns FALSE when the process has no console");
    r = CALL(kernel32, AllocConsole);
    T_EQ(r, FALSE);

    T_CASE("returns FALSE on the second of two calls in a row");
    CALL(kernel32, AllocConsole);
    r = CALL(kernel32, AllocConsole);
    T_EQ(r, FALSE);

    T_CASE("sets ERROR_ACCESS_DENIED");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, AllocConsole);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);

    T_CASE("logs that there is no console");
    r = CALL(kernel32, AllocConsole);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "AllocConsole: there is no console");
}
