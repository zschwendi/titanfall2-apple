#include <stdint.h>
#include <string.h>
#include "rt/test.h"

/* Windows (imm32.c test_InvalidIMC, imc_bad 0xdeadcafe): ImmGetCandidateListW gives
 * ERROR_INVALID_HANDLE for a handle that is not an input context; a NULL one is a getter
 * and keeps last error. */
TEST(imm32, ImmGetCandidateListW)
{
    unsigned char buf[16], want[16];
    memset(want, 0xA5, sizeof want);

    T_CASE("NULL input context");
    memset(buf, 0xA5, sizeof buf);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmGetCandidateListW, NULL, 0, buf, (DWORD)sizeof buf), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(memcmp(buf, want, sizeof buf) == 0);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetCandidateListW: there is no input method"));

    T_CASE("invalid input context");
    memset(buf, 0xA5, sizeof buf);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmGetCandidateListW, (HIMC)(uintptr_t)0x1234, 1, buf, (DWORD)sizeof buf), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(memcmp(buf, want, sizeof buf) == 0);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetCandidateListW: there is no input method"));
}
