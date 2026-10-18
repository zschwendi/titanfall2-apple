/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immgetconversionstatus
 * RT-CASES:  NULL input context returns FALSE; NULL input context keeps last error; an input context never handed out returns FALSE; an input context never handed out sets ERROR_INVALID_HANDLE; logs that there is no input method; writes no output
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(imm32, BOOL, ImmGetConversionStatus, HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    /* This machine has no input method (US English keyboard layout, docs/RUNTIME.md 8.4 and 9.2):
     * no input context was ever handed out, so every HIMC is NULL or invalid. Nothing is looked
     * at except hIMC, and nothing is written. A NULL hIMC is a getter that keeps last error; any
     * other hIMC fails with ERROR_INVALID_HANDLE (Wine imm32.c test_InvalidIMC). */
    (void)lpfdwConversion;
    (void)lpfdwSentence;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmGetConversionStatus", "ImmGetConversionStatus: there is no input method");
    if (!hIMC) return FALSE;
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
