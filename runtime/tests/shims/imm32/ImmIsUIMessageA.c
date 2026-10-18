#include "rt/test.h"
#include "rt/win/user.h"

TEST(imm32, ImmIsUIMessageA)
{
    T_CASE("a message that is not an input-method message returns FALSE");
    T_EQ(CALL(imm32, ImmIsUIMessageA, NULL, WM_NULL, 0, 0), FALSE);
    T_EQ(CALL(imm32, ImmIsUIMessageA, NULL, WM_CHAR, 0, 0), FALSE);
    T_EQ(CALL(imm32, ImmIsUIMessageA, NULL, WM_USER, 0, 0), FALSE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(imm32, ImmIsUIMessageA, NULL, WM_CHAR, 0, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-CASES: one of the eight with a NULL window gives TRUE, and leaves last error alone. */
    T_CASE("one of the eight with a NULL window returns TRUE");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmIsUIMessageA, NULL, WM_IME_COMPOSITION, 0, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: an input-method message for a window is a stop, not a FALSE: nothing sends one
     * here. A window is any non-NULL handle; the shim does not use its value. */
    HWND win = (HWND)(uintptr_t)0x1234;
    T_TRAPS(CALL(imm32, ImmIsUIMessageA, win, WM_IME_COMPOSITION, 0, 0), "an input-method message 0x10f for window");
    T_TRAPS(CALL(imm32, ImmIsUIMessageA, win, WM_IME_SETCONTEXT, 0, 0), "an input-method message 0x281 for window");

    /* WM_IME_SYSTEM (0x287) is an input-method message too: Windows gives TRUE for it, never FALSE
     * (Wine imm32 tests, test_ImmIsUIMessage; review C7). With a window it is a stop, like the seven. */
    T_TRAPS(CALL(imm32, ImmIsUIMessageA, win, WM_IME_SYSTEM, 0, 0), "an input-method message 0x287 for window");
}
