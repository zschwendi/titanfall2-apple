/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immgetproperty
 * RT-CASES:  every layout and index answers 0; logs that there is no input method; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(imm32, DWORD, ImmGetProperty, HKL hKL, DWORD fdwIndex)
{
    /* This machine has no input method (US English layout, docs/RUNTIME.md 8.4 and 9.2), so
     * there is no input context and no layout to describe: the answer is 0 for every layout
     * and every index, and neither argument is looked at. */
    (void)hKL;
    (void)fdwIndex;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmGetProperty", "ImmGetProperty: there is no input method");
    return 0;
}
