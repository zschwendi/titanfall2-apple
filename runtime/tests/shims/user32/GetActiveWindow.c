#include "rt/test.h"

TEST(user32, GetActiveWindow)
{
    T_CASE("NULL until a window has been activated");
    T_EQ(CALL(user32, GetActiveWindow), NULL);
    T_EQ(CALL(user32, GetActiveWindow), NULL);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetActiveWindow);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
