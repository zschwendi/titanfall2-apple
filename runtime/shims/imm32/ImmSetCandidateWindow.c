/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/imm/nf-imm-immsetcandidatewindow
 * RT-CASES:  NULL HIMC; invalid HIMC; NULL CANDIDATEFORM
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/user.h"

SHIM(imm32, BOOL, ImmSetCandidateWindow, HIMC hIMC, LPCANDIDATEFORM lpCandidate)
{
    /* absent: this machine has no input method, so no input context and no HIMC ever exists.
     * Every HIMC a caller passes is NULL or invalid. The CANDIDATEFORM is not read, and nothing
     * is written through it. A non-NULL HIMC fails with ERROR_INVALID_HANDLE. A NULL HIMC leaves
     * last error as it was: Windows' value for that case is not established yet. The documented
     * failure return is 0. */
    (void)lpCandidate;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "imm32.dll!ImmSetCandidateWindow", "ImmSetCandidateWindow: there is no input method");
    if (!hIMC) return FALSE;
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
