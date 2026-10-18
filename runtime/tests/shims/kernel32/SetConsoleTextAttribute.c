#include "rt/test.h"
#include "rt/file.h"
#include "rt/log.h"
#include "rt/win/file.h"

/* The runtime has no console, so every call fails. */
TEST(kernel32, SetConsoleTextAttribute)
{
    HANDLE std_out;
    BOOL r;

    T_CASE("fails for a NULL handle");
    r = CALL(kernel32, SetConsoleTextAttribute, NULL, 0x0F);
    T_EQ(r, FALSE);

    T_CASE("fails for a handle that is not a console");
    r = CALL(kernel32, SetConsoleTextAttribute, (HANDLE)0x44, 0x0F);
    T_EQ(r, FALSE);

    T_CASE("fails for the standard output handle");
    std_out = rt_std_get(STD_OUTPUT_HANDLE);
    r = CALL(kernel32, SetConsoleTextAttribute, std_out, 0x0F);
    T_EQ(r, FALSE);

    T_CASE("fails for any attribute value");
    r = CALL(kernel32, SetConsoleTextAttribute, (HANDLE)0x44, 0);
    T_EQ(r, FALSE);
    r = CALL(kernel32, SetConsoleTextAttribute, (HANDLE)0x44, 0xFFFF);
    T_EQ(r, FALSE);

    T_CASE("sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, SetConsoleTextAttribute, NULL, 0x0F);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no console");
    r = CALL(kernel32, SetConsoleTextAttribute, (HANDLE)0x44, 0x0F);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "SetConsoleTextAttribute: there is no console");
}
