/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/getconsolemode
 * RT-CASES:  fails for a NULL handle; fails for a handle that is not a console; fails with a NULL lpMode; leaves a preset lpMode alone; sets ERROR_INVALID_HANDLE; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, GetConsoleMode, HANDLE hConsoleHandle, LPDWORD lpMode)
{
    /* This runtime has no console, and the standard handles are character devices that are not
     * consoles, so the call fails for every handle, as Windows fails for any handle when the
     * process has no console (the file contract's decision). No argument is looked at and
     * lpMode is not written. */
    (void)hConsoleHandle;
    (void)lpMode;
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!GetConsoleMode", "GetConsoleMode: there is no console");
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
