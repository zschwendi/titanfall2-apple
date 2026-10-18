#include "rt/test.h"

TEST(user32, GetKeyboardLayout)
{
    T_CASE("idThread 0");
    T_EQ(CALL(user32, GetKeyboardLayout, 0), (HKL)0x04090409);

    T_CASE("idThread of the calling thread");
    T_EQ(CALL(user32, GetKeyboardLayout, CALL(kernel32, GetCurrentThreadId)), (HKL)0x04090409);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetKeyboardLayout, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the layout of another thread is never guessed. 4096 must not be the calling
     * thread's id: the check next to it proves that. */
    T_TRUE(CALL(kernel32, GetCurrentThreadId) != 4096);
    T_TRAPS(CALL(user32, GetKeyboardLayout, 4096), "keyboard layout of another thread: 4096");
}
