#include "rt/test.h"

TEST(user32, GetForegroundWindow)
{
    T_CASE("NULL before a window is activated, on two calls");
    T_EQ(CALL(user32, GetForegroundWindow), NULL);
    T_EQ(CALL(user32, GetForegroundWindow), NULL);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetForegroundWindow);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
