/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immgetcontext
 * RT-CASES:  every window: NULL, with the log line; writes no byte through a pointer; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(imm32, HIMC, ImmGetContext, HWND hWnd)
{
    /* This machine has no input method (docs/RUNTIME.md 8.4 and 9.2): no input context exists and
     * no HIMC is ever handed out, so the answer is NULL for every window. hWnd is not looked at. */
    (void)hWnd;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmGetContext", "ImmGetContext: there is no input method");
    return NULL;
}
