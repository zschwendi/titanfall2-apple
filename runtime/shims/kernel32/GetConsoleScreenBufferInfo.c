/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/getconsolescreenbufferinfo
 * RT-CASES:  fails for a NULL handle; fails for a handle that is not a console; fails for the standard output handle; fails with a NULL lpConsoleScreenBufferInfo; leaves a preset structure alone; sets ERROR_INVALID_HANDLE; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/file.h"

SHIM(kernel32, BOOL, GetConsoleScreenBufferInfo, HANDLE hConsoleOutput, PCONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo)
{
    /* This runtime has no console, and the standard handles are character devices that are not
     * consoles, so the call fails for every handle, as Windows fails for any handle when the
     * process has no console (the file contract's decision). No argument is looked at and
     * lpConsoleScreenBufferInfo is not written. */
    (void)hConsoleOutput;
    (void)lpConsoleScreenBufferInfo;
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!GetConsoleScreenBufferInfo", "GetConsoleScreenBufferInfo: there is no console");
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
