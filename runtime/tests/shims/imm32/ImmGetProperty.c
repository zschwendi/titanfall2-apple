#include "rt/test.h"

TEST(imm32, ImmGetProperty)
{
    /* rt/win/ defines no IGP_ index, so the indexes are plain numbers; the all-ones value is
     * included on purpose. */
    T_CASE("every layout and index answers 0");
    T_EQ(CALL(imm32, ImmGetProperty, (HKL)0, 0), 0);
    T_EQ(CALL(imm32, ImmGetProperty, (HKL)0, 1), 0);
    T_EQ(CALL(imm32, ImmGetProperty, (HKL)0, 0x10), 0);
    T_EQ(CALL(imm32, ImmGetProperty, (HKL)0, (DWORD)-1), 0);
    T_EQ(CALL(imm32, ImmGetProperty, (HKL)(uintptr_t)0x04090409u, 0), 0);
    T_EQ(CALL(imm32, ImmGetProperty, (HKL)(uintptr_t)0x04090409u, 0x10), 0);
    T_EQ(CALL(imm32, ImmGetProperty, (HKL)(uintptr_t)-1, (DWORD)-1), 0);

    T_CASE("logs that there is no input method");
    CALL(imm32, ImmGetProperty, (HKL)0, 0);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetProperty: there is no input method"));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(imm32, ImmGetProperty, (HKL)0, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
