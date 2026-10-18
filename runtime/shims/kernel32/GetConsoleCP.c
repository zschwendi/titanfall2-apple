/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/getconsolecp
 * RT-CASES:  returns 0 when the process has no console; sets ERROR_INVALID_HANDLE; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, UINT, GetConsoleCP)
{
    /* This runtime has no console, so there is no console input code page: the call returns 0,
     * the documented failure value (the file contract's decision). */
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!GetConsoleCP", "GetConsoleCP: there is no console");
    return FAIL(ctx, ERROR_INVALID_HANDLE, 0);
}
