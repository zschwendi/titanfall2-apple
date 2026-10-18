#include "rt/test.h"
#include "rt/window.h"

TEST(user32, GetProcessWindowStation)
{
    T_CASE("returns the window station handle");
    T_EQ(CALL(user32, GetProcessWindowStation), RT_HWINSTA);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetProcessWindowStation);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
