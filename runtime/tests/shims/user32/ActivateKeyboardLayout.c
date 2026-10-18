#include "rt/test.h"
#include "rt/window.h"

TEST(user32, ActivateKeyboardLayout)
{
    T_CASE("RT_HKL_US, HKL_NEXT or HKL_PREV: the layout stays, returns RT_HKL_US");
    T_EQ(CALL(user32, ActivateKeyboardLayout, RT_HKL_US, 0), RT_HKL_US);
    T_EQ(CALL(user32, ActivateKeyboardLayout, (HKL)1, 0), RT_HKL_US);
    T_EQ(CALL(user32, ActivateKeyboardLayout, (HKL)0, 0), RT_HKL_US);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, ActivateKeyboardLayout, RT_HKL_US, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a handle other than the three accepted ones is never guessed. 0x04070407 is
     * written as a number: it is not a constant the runtime defines (a layout it does not have). */
    T_TRAPS(CALL(user32, ActivateKeyboardLayout, (HKL)0x04070407, 0), "ActivateKeyboardLayout with handle");
}
