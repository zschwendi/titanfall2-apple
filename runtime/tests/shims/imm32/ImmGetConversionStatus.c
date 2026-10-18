#include <string.h>
#include "rt/test.h"

/* Windows (imm32.c test_InvalidIMC, imc_bad 0xdeadcafe): ImmGetConversionStatus gives
 * ERROR_INVALID_HANDLE for a handle that is not an input context. A NULL one is a getter and
 * keeps last error (the rule for getters; the Wine test read does not call it with NULL). */
TEST(imm32, ImmGetConversionStatus)
{
    DWORD conv, sent;
    HIMC never = (HIMC)(uintptr_t)0x1234;   /* an input context that was never handed out */

    T_CASE("NULL input context returns FALSE");
    T_EQ(CALL(imm32, ImmGetConversionStatus, NULL, &conv, &sent), FALSE);

    T_CASE("NULL input context keeps last error");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    CALL(imm32, ImmGetConversionStatus, NULL, &conv, &sent);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    T_CASE("an input context never handed out returns FALSE");
    T_EQ(CALL(imm32, ImmGetConversionStatus, never, &conv, &sent), FALSE);
    T_EQ(CALL(imm32, ImmGetConversionStatus, never, NULL, NULL), FALSE);

    T_CASE("an input context never handed out sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    CALL(imm32, ImmGetConversionStatus, never, &conv, &sent);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("logs that there is no input method");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetConversionStatus: there is no input method"));

    T_CASE("writes no output");
    memset(&conv, 0xA5, sizeof conv);
    memset(&sent, 0xA5, sizeof sent);
    CALL(imm32, ImmGetConversionStatus, never, &conv, &sent);
    T_EQ(conv, 0xA5A5A5A5u);
    T_EQ(sent, 0xA5A5A5A5u);
}
