#include "rt/test.h"
#include "rt/window.h"

TEST(user32, LoadKeyboardLayoutA)
{
    T_CASE("the name 00000409: returns RT_HKL_US");
    T_EQ(CALL(user32, LoadKeyboardLayoutA, "00000409", 0), RT_HKL_US);
    T_EQ(CALL(user32, LoadKeyboardLayoutA, "00000409", 1), RT_HKL_US);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, LoadKeyboardLayoutA, "00000409", 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a name other than "00000409", and NULL, is never guessed: the run stops with
     * the name. "00000407" is only an example of another name. */
    T_TRAPS(CALL(user32, LoadKeyboardLayoutA, "00000407", 0), "LoadKeyboardLayoutA with name 00000407");
    T_TRAPS(CALL(user32, LoadKeyboardLayoutA, NULL, 0), "LoadKeyboardLayoutA with name NULL");
}
