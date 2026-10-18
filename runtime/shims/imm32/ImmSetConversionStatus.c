/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immsetconversionstatus
 * RT-CASES:  NULL input context returns FALSE; NULL input context sets ERROR_INVALID_HANDLE; an input context never handed out returns FALSE; an input context never handed out sets ERROR_INVALID_HANDLE; logs that there is no input method
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(imm32, BOOL, ImmSetConversionStatus, HIMC hIMC, DWORD fdwConversion, DWORD fdwSentence)
{
    /* This machine has no input method (US English keyboard layout, docs/RUNTIME.md 8.4 and 9.2):
     * no input context was ever handed out, so every HIMC is NULL or invalid. Nothing is looked
     * at. For every HIMC the answer is FALSE with ERROR_INVALID_HANDLE (Wine imm32.c
     * test_InvalidIMC, imc_null and imc_bad). */
    (void)hIMC;
    (void)fdwConversion;
    (void)fdwSentence;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmSetConversionStatus", "ImmSetConversionStatus: there is no input method");
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
