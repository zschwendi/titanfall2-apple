#include "rt/test.h"
#include "rt/log.h"

/* The runtime has no console, so every call fails and writes nothing through lpMode. */
TEST(kernel32, GetConsoleMode)
{
    DWORD mode;
    BOOL r;

    T_CASE("fails for a NULL handle");
    mode = 0;
    r = CALL(kernel32, GetConsoleMode, NULL, &mode);
    T_EQ(r, FALSE);

    T_CASE("fails for a handle that is not a console");
    r = CALL(kernel32, GetConsoleMode, (HANDLE)0x44, &mode);
    T_EQ(r, FALSE);

    T_CASE("fails with a NULL lpMode");
    r = CALL(kernel32, GetConsoleMode, (HANDLE)0x44, NULL);
    T_EQ(r, FALSE);

    T_CASE("leaves a preset lpMode alone");
    mode = 0xA5A5A5A5;
    r = CALL(kernel32, GetConsoleMode, (HANDLE)0x44, &mode);
    T_EQ(r, FALSE);
    T_EQ(mode, 0xA5A5A5A5);

    T_CASE("sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, GetConsoleMode, NULL, &mode);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no console");
    r = CALL(kernel32, GetConsoleMode, (HANDLE)0x44, &mode);
    T_EQ(r, FALSE);
    T_LOGGED(RT_CH_FILE, "GetConsoleMode: there is no console");
}
