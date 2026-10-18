#include "rt/test.h"

/* Windows (imm32.c test_InvalidIMC, imc_null and imc_bad): ImmSetConversionStatus gives
 * ERROR_INVALID_HANDLE for a NULL handle and for a handle that is not an input context. */
TEST(imm32, ImmSetConversionStatus)
{
    HIMC never = (HIMC)(uintptr_t)0x1234;   /* an input context that was never handed out */

    T_CASE("NULL input context returns FALSE");
    T_EQ(CALL(imm32, ImmSetConversionStatus, NULL, 0x1, 0x2), FALSE);

    T_CASE("NULL input context sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(imm32, ImmSetConversionStatus, NULL, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("an input context never handed out returns FALSE");
    T_EQ(CALL(imm32, ImmSetConversionStatus, never, 0xFFFFFFFFu, 0), FALSE);

    T_CASE("an input context never handed out sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    CALL(imm32, ImmSetConversionStatus, never, 0, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no input method");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmSetConversionStatus: there is no input method"));
}
