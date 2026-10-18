#include "rt/test.h"

/* The core answers 0 for every key 0 to 255: up, and not toggled.  0x10 is VK_SHIFT and 0x14 is
 * VK_CAPITAL; those names are not in runtime/include/rt/win/, so the values are written. */
TEST(user32, GetKeyState)
{
    T_CASE("returns 0 for keys 0, 0x10, 0x14, 0x41 and 255");
    T_EQ(CALL(user32, GetKeyState, 0), 0);
    T_EQ(CALL(user32, GetKeyState, 0x10), 0);       /* VK_SHIFT */
    T_EQ(CALL(user32, GetKeyState, 0x14), 0);       /* VK_CAPITAL */
    T_EQ(CALL(user32, GetKeyState, 0x41), 0);       /* 'A' */
    T_EQ(CALL(user32, GetKeyState, 255), 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetKeyState, 0x41);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stop for a key outside 0-255 (RT-TRAPS: core: vk outside 0-255). */
    T_TRAPS(CALL(user32, GetKeyState, 256), "rt_key_state: not implemented");
    T_TRAPS(CALL(user32, GetKeyState, -1), "rt_key_state: not implemented");
}
