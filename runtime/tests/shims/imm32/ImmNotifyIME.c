#include "rt/test.h"

/* Windows (imm32.c test_ImmNotifyIME): a NULL HIMC gives 0 with last error ERROR_SUCCESS; a bad
 * HIMC gives 0 with ERROR_INVALID_HANDLE. The arguments are not read, so any values give the same
 * answer. The calls pass 0 for them: NI_COMPOSITIONSTR and CPS_CANCEL are not defined under
 * runtime/include/rt/win/, and the shim does not read them. */
TEST(imm32, ImmNotifyIME)
{
    T_CASE("NULL HIMC");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(imm32, ImmNotifyIME, NULL, 0, 0, 0), FALSE);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmNotifyIME: there is no input method"));
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("invalid HIMC");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(imm32, ImmNotifyIME, (HIMC)(uintptr_t)0x1234, (DWORD)-1, (DWORD)-1, (DWORD)-1), FALSE);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmNotifyIME: there is no input method"));
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
}
