#include "rt/test.h"
#include "rt/gdi.h"
#include "rt/window.h"
#include "rt/win/error.h"
#include "rt/win/user.h"

TEST(user32, GetDC)
{
    HDC dc;

    T_CASE("NULL gives a display context of the screen");
    CALL(kernel32, SetLastError, 0x1234);
    dc = CALL(user32, GetDC, NULL);
    T_TRUE(dc != NULL);
    T_TRUE((uintptr_t)dc >= 0x40000000u);
    T_EQ(rt_dc_device_cap(dc, HORZRES), 1920);
    T_EQ(CALL(user32, ReleaseDC, NULL, dc), 1);

    T_CASE("a handle that is not a window gives NULL with ERROR_INVALID_WINDOW_HANDLE");
    CALL(kernel32, SetLastError, 0);
    dc = CALL(user32, GetDC, (HWND)(uintptr_t)0x12345);
    T_EQ(dc, NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
}
