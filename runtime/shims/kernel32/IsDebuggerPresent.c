/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/debugapi/nf-debugapi-isdebuggerpresent
 * RT-CASES:  returns FALSE in a normal run; agrees with the BeingDebugged byte of the process block; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, IsDebuggerPresent)
{
    /* Windows reads this byte of the process block; so do we, so the two cannot disagree.
     * The runtime sets it to 0 at start-up (docs/RUNTIME.md section 9.4). */
    return LD8(LD64(ctx->gs_base + RT_TEB_PEB) + RT_PEB_BEING_DEBUGGED) != 0;
}
