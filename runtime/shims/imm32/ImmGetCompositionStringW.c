/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immgetcompositionstringw
 * RT-CASES:  returns 0 for a NULL input context; NULL input context keeps last error; returns 0 for an input context never handed out; an input context never handed out sets ERROR_INVALID_HANDLE; returns 0 for every index and any buffer; leaves the output buffer alone; logs that there is no input method
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(imm32, LONG, ImmGetCompositionStringW, HIMC hIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen)
{
    /* This runtime has no input method: the keyboard layout is US English (docs/RUNTIME.md 8.4
     * and 9.2), so no input context exists and no HIMC was ever handed out. Every HIMC a caller
     * passes is NULL or invalid. Only hIMC is looked at, and nothing is written through lpBuf.
     * A NULL hIMC is a getter that keeps last error; any other hIMC fails with ERROR_INVALID_HANDLE
     * (Wine imm32.c test_InvalidIMC). The answer is 0, as the notes for this shim say.
     * The documentation lists negative codes (IMM_ERROR_NODATA, IMM_ERROR_GENERAL) for a call
     * that does not succeed; this answer does not use them. */
    (void)dwIndex;
    (void)lpBuf;
    (void)dwBufLen;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmGetCompositionStringW", "ImmGetCompositionStringW: there is no input method");
    if (!hIMC) return 0;
    return FAIL(ctx, ERROR_INVALID_HANDLE, 0);
}
