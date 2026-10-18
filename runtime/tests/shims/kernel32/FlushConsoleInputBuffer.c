#include "rt/test.h"
#include "rt/file.h"
#include "rt/log.h"

/* The runtime has no console, so every call fails as Windows fails for a handle that is not a
 * console's. No pointer argument is written. */
TEST(kernel32, FlushConsoleInputBuffer)
{
    HANDLE std_in;
    BOOL r;

    std_in = rt_std_get(STD_INPUT_HANDLE);

    T_CASE("fails for a NULL handle");
    r = CALL(kernel32, FlushConsoleInputBuffer, NULL);
    T_EQ(r, FALSE);

    T_CASE("fails for a handle that is not a console");
    r = CALL(kernel32, FlushConsoleInputBuffer, (HANDLE)0x44);
    T_EQ(r, FALSE);

    T_CASE("fails for the standard input handle");
    r = CALL(kernel32, FlushConsoleInputBuffer, std_in);
    T_EQ(r, FALSE);

    T_CASE("sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, FlushConsoleInputBuffer, (HANDLE)0x44);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no console");
    r = CALL(kernel32, FlushConsoleInputBuffer, NULL);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "FlushConsoleInputBuffer: there is no console");
}
