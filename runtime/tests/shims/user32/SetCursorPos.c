#include "rt/test.h"

/* The position is read back with GetCursorPos, which is what the guest sees.  Each test runs
 * in its own process: the core's position starts at {960, 540}. */
TEST(user32, SetCursorPos)
{
    POINT p;

    T_CASE("moves the cursor to the given position");
    T_EQ(CALL(user32, SetCursorPos, 100, 200), TRUE);
    p.x = -1;
    p.y = -1;
    T_EQ(CALL(user32, GetCursorPos, &p), TRUE);
    T_EQ(p.x, 100);
    T_EQ(p.y, 200);

    T_CASE("clamps a position outside the screen to the nearest point inside");
    T_EQ(CALL(user32, SetCursorPos, 5000, -7), TRUE);
    p.x = -1;
    p.y = -1;
    T_EQ(CALL(user32, GetCursorPos, &p), TRUE);
    T_EQ(p.x, 1919);                    /* the right edge lies outside the screen: right - 1 */
    T_EQ(p.y, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, SetCursorPos, 100, 200), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
