/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immnotifyime
 * RT-CASES:  NULL HIMC; invalid HIMC
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(imm32, BOOL, ImmNotifyIME, HIMC hIMC, DWORD dwAction, DWORD dwIndex, DWORD dwValue)
{
    /* absent: this machine has no input method, so no input context and no HIMC ever exists.
     * Every HIMC a caller passes is NULL or invalid. No argument other than hIMC is read. The
     * documented failure return is 0. Windows (Wine imm32.c test_ImmNotifyIME): a NULL HIMC gives
     * last error ERROR_SUCCESS, so it is set to 0 here; any other HIMC gives ERROR_INVALID_HANDLE. */
    (void)dwAction;
    (void)dwIndex;
    (void)dwValue;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmNotifyIME", "ImmNotifyIME: there is no input method");
    if (!hIMC) {
        rt_set_last_error(ctx, ERROR_SUCCESS);
        return FALSE;
    }
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
