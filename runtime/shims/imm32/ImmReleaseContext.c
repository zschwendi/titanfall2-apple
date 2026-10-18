/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immreleasecontext
 * RT-CASES:  every argument: TRUE, with the log line; writes no byte through a pointer; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(imm32, BOOL, ImmReleaseContext, HWND hWnd, HIMC hIMC)
{
    /* This machine has no input method (docs/RUNTIME.md 8.4 and 9.2): no input context exists and
     * no HIMC is ever handed out, so there is never one to release. The answer is TRUE for every
     * argument, and neither argument is looked at. */
    (void)hWnd;
    (void)hIMC;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmReleaseContext", "ImmReleaseContext: there is no input method");
    return TRUE;
}
