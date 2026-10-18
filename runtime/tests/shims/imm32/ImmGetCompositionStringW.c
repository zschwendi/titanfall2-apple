#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/log.h"

/* The runtime has no input method, so no input context exists: every call returns 0 and
 * writes nothing through the buffer. Last error: a NULL input context keeps it; any other
 * input context fails with ERROR_INVALID_HANDLE (Wine imm32.c test_InvalidIMC: imc_bad 0xdeadcafe,
 * ImmGetCompositionStringW ERROR_INVALID_HANDLE). */
TEST(imm32, ImmGetCompositionStringW)
{
    unsigned char buf[64];
    const unsigned char *p = buf;
    int i, bad;

    T_CASE("returns 0 for a NULL input context");
    T_EQ(CALL(imm32, ImmGetCompositionStringW, NULL, 0x0008, buf, sizeof buf), 0);

    T_CASE("NULL input context keeps last error");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(imm32, ImmGetCompositionStringW, NULL, 0x0008, buf, sizeof buf), 0);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    T_CASE("returns 0 for an input context never handed out");
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)0x44, 0x0008, buf, sizeof buf), 0);
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)0x1000, 0x0800, buf, sizeof buf), 0);

    T_CASE("an input context never handed out sets ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)(uintptr_t)0xdeadcafe, 0x0008, buf, sizeof buf), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)0x44, 0x0008, NULL, 0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("returns 0 for every index and any buffer");
    T_EQ(CALL(imm32, ImmGetCompositionStringW, NULL, 0, NULL, 0), 0);
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)0x44, 0xFFFFFFFF, NULL, 0), 0);
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)0x44, 0x0800, buf, 0), 0);

    T_CASE("leaves the output buffer alone");
    memset(buf, 0xA5, sizeof buf);
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)0x44, 0x0008, buf, sizeof buf), 0);
    bad = 0;
    for (i = 0; i < (int)sizeof buf; i++)
        if (p[i] != 0xA5) bad++;
    T_EQ(bad, 0);

    T_CASE("logs that there is no input method");
    T_EQ(CALL(imm32, ImmGetCompositionStringW, (HIMC)0x44, 0x0008, buf, sizeof buf), 0);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmGetCompositionStringW: there is no input method"));
}
