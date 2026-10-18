#include "rt/test.h"

TEST(user32, GetShellWindow)
{
    T_CASE("no shell process: NULL");
    T_EQ(CALL(user32, GetShellWindow), NULL);
    T_EQ(CALL(user32, GetShellWindow), NULL);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetShellWindow), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, GetShellWindow), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
