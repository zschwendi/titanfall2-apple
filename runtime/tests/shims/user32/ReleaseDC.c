#include "rt/test.h"
#include "rt/gdi.h"
#include "rt/window.h"

TEST(user32, ReleaseDC)
{
    HDC dc, mem;

    T_CASE("releases a context from GetDC once, a second release fails");
    dc = CALL(user32, GetDC, NULL);
    T_TRUE(dc != NULL);
    T_EQ(CALL(user32, ReleaseDC, NULL, dc), 1);
    T_EQ(CALL(user32, ReleaseDC, NULL, dc), 0);

    T_CASE("the window is not compared");
    dc = CALL(user32, GetDC, NULL);
    T_TRUE(dc != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, ReleaseDC, RT_HWND_DESKTOP, dc), 1);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL and a context GetDC did not give out fail");
    T_EQ(CALL(user32, ReleaseDC, NULL, NULL), 0);
    mem = rt_dc_create_compatible(NULL);
    T_TRUE(mem != NULL);
    T_EQ(CALL(user32, ReleaseDC, NULL, mem), 0);
}
