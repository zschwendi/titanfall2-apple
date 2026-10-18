#include <stdint.h>
#include <string.h>
#include "rt/test.h"

/* Windows (imm32.c test_InvalidIMC, imc_bad 0xdeadcafe): ImmGetCandidateListCountW gives
 * ERROR_INVALID_HANDLE for a handle that is not an input context; a NULL one is a getter
 * and keeps last error. */
TEST(imm32, ImmGetCandidateListCountW)
{
    DWORD count;

    T_CASE("NULL input context");
    memset(&count, 0xA5, sizeof count);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmGetCandidateListCountW, NULL, &count), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(count, 0xA5A5A5A5u);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetCandidateListCountW: there is no input method"));

    T_CASE("invalid input context");
    memset(&count, 0xA5, sizeof count);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmGetCandidateListCountW, (HIMC)(uintptr_t)0x1234, &count), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(count, 0xA5A5A5A5u);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetCandidateListCountW: there is no input method"));
}
