#include "rt/test.h"
#include "rt/window.h"

TEST(user32, WindowFromPoint)
{
    T_CASE("points inside the desktop give the desktop window");
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ 0, 0 }), RT_HWND_DESKTOP);
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ 1919, 1079 }), RT_HWND_DESKTOP);
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ 960, 540 }), RT_HWND_DESKTOP);

    T_CASE("points outside the desktop give NULL");
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ 1920, 0 }), NULL);
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ 0, 1080 }), NULL);
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ -1, 5 }), NULL);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ 0, 0 }), RT_HWND_DESKTOP);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, WindowFromPoint, (POINT){ 1920, 0 }), NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
