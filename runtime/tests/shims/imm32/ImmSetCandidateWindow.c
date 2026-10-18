#include <stdint.h>
#include "rt/test.h"
#include "rt/win/user.h"

/* 1 if the n bytes at p all still hold 0xA5. */
static int still_a5(const unsigned char *p, int n)
{
    for (int i = 0; i < n; i++)
        if (p[i] != 0xA5) return 0;
    return 1;
}

/* A non-NULL HIMC gives FALSE with ERROR_INVALID_HANDLE (the rule for a non-NULL HIMC; this
 * function is not among the Wine imm32.c tests read). The NULL HIMC case below keeps the
 * last error the shim leaves: Windows' value for it is not established yet. */
TEST(imm32, ImmSetCandidateWindow)
{
    /* The CANDIDATEFORM is not read, and nothing is written through it. */
    unsigned char buf[64];
    LPCANDIDATEFORM form = (LPCANDIDATEFORM)buf;
    HIMC bad = (HIMC)(uintptr_t)0x1234;

    T_CASE("NULL HIMC");
    for (int i = 0; i < 64; i++) buf[i] = 0xA5;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmSetCandidateWindow, NULL, form), FALSE);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmSetCandidateWindow: there is no input method"));
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(still_a5(buf, 64));

    T_CASE("invalid HIMC");
    for (int i = 0; i < 64; i++) buf[i] = 0xA5;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmSetCandidateWindow, bad, form), FALSE);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmSetCandidateWindow: there is no input method"));
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(still_a5(buf, 64));

    T_CASE("NULL CANDIDATEFORM");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(imm32, ImmSetCandidateWindow, bad, NULL), FALSE);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "ImmSetCandidateWindow: there is no input method"));
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
}
