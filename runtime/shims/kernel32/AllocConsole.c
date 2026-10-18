/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/allocconsole
 * RT-CASES:  returns FALSE when the process has no console; returns FALSE on the second of two calls in a row; sets ERROR_ACCESS_DENIED; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, AllocConsole)
{
    /* This runtime has no console, so the process never gets one: the call fails as Windows
     * fails for a process that already has a console (the file contract's decision). */
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!AllocConsole", "AllocConsole: there is no console");
    return FAIL(ctx, ERROR_ACCESS_DENIED, FALSE);
}
