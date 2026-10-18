#include "rt/test.h"

TEST(imm32, ImmReleaseContext)
{
    T_CASE("every argument: TRUE, with the log line");
    T_EQ(CALL(imm32, ImmReleaseContext, NULL, NULL), TRUE);
    T_EQ(CALL(imm32, ImmReleaseContext, (HWND)(uintptr_t)0x1234, NULL), TRUE);
    T_EQ(CALL(imm32, ImmReleaseContext, NULL, (HIMC)(uintptr_t)0x1234), TRUE);
    T_EQ(CALL(imm32, ImmReleaseContext, (HWND)(uintptr_t)0x1234, (HIMC)(uintptr_t)0xFFFFFFFFFFFFFFFFull), TRUE);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmReleaseContext: there is no input method"));

    T_CASE("writes no byte through a pointer");
    {
        /* The buffer is passed as both arguments: a shim that wrote through them would change it. */
        unsigned char buf[32];
        int i, same = 1;
        for (i = 0; i < 32; i++) buf[i] = 0xA5;
        T_EQ(CALL(imm32, ImmReleaseContext, (HWND)buf, (HIMC)buf), TRUE);
        for (i = 0; i < 32; i++) if (buf[i] != 0xA5) same = 0;
        T_TRUE(same);
    }

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(imm32, ImmReleaseContext, NULL, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
