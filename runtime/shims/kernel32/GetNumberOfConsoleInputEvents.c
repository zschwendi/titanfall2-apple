/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/getnumberofconsoleinputevents
 * RT-CASES:  fails for a NULL handle; fails for a handle that is not a console; fails for the standard input handle; fails with a NULL lpcNumberOfEvents; leaves a preset count alone; sets ERROR_INVALID_HANDLE; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, GetNumberOfConsoleInputEvents, HANDLE hConsoleInput, LPDWORD lpcNumberOfEvents)
{
    /* This runtime has no console, and the standard handles are not consoles, so the call fails
     * for every handle, as Windows fails for any handle when the process has no console (the file
     * contract's decision). No argument is looked at and lpcNumberOfEvents is not written. */
    (void)hConsoleInput;
    (void)lpcNumberOfEvents;
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!GetNumberOfConsoleInputEvents", "GetNumberOfConsoleInputEvents: there is no console");
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
