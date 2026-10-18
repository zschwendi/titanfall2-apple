/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immgetcandidatelistcountw
 * RT-CASES:  NULL input context; invalid input context
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

/* This machine has no input method (docs/RUNTIME.md 8.4, 9.2): no HIMC was ever handed out,
 * so every HIMC is NULL or invalid. Only hIMC is looked at; nothing is written, lpdwListCount
 * included. A NULL hIMC is a getter that keeps last error; any other hIMC fails with
 * ERROR_INVALID_HANDLE (Wine imm32.c test_InvalidIMC). The documented failure return is 0. */
SHIM(imm32, DWORD, ImmGetCandidateListCountW, HIMC hIMC, LPDWORD lpdwListCount)
{
    (void)lpdwListCount;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmGetCandidateListCountW", "ImmGetCandidateListCountW: there is no input method");
    if (!hIMC) return 0;
    return FAIL(ctx, ERROR_INVALID_HANDLE, 0);
}
