#include "rt/test.h"

TEST(imm32, ImmGetContext)
{
    T_CASE("every window: NULL, with the log line");
    T_TRUE(CALL(imm32, ImmGetContext, NULL) == NULL);
    T_TRUE(CALL(imm32, ImmGetContext, (HWND)(uintptr_t)0x1234) == NULL);
    T_TRUE(CALL(imm32, ImmGetContext, (HWND)(uintptr_t)0xFFFFFFFFFFFFFFFFull) == NULL);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetContext: there is no input method"));

    T_CASE("writes no byte through a pointer");
    {
        /* The buffer is passed as the window: a shim that wrote through its argument would change it. */
        unsigned char buf[32];
        int i, same = 1;
        for (i = 0; i < 32; i++) buf[i] = 0xA5;
        T_TRUE(CALL(imm32, ImmGetContext, (HWND)buf) == NULL);
        for (i = 0; i < 32; i++) if (buf[i] != 0xA5) same = 0;
        T_TRUE(same);
    }

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(imm32, ImmGetContext, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
