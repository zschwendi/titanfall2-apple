#include "rt/test.h"
#include "rt/window.h"

TEST(user32, GetDesktopWindow)
{
    T_CASE("the desktop window");
    T_EQ(CALL(user32, GetDesktopWindow), RT_HWND_DESKTOP);
    T_EQ(CALL(user32, GetDesktopWindow), RT_HWND_DESKTOP);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetDesktopWindow), RT_HWND_DESKTOP);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, GetDesktopWindow), RT_HWND_DESKTOP);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
