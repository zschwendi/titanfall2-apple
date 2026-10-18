/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/writeconsole
 * RT-CASES:  fails with ERROR_INVALID_HANDLE when there is no console; writes neither the count nor the buffer; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/log.h"

SHIM(kernel32, BOOL, WriteConsoleW, HANDLE hConsoleOutput, LPCVOID lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved)
{
    /* This runtime has no console (docs/RUNTIME.md 7.3): the call fails as Windows fails when
     * the process has no console. No argument is looked at and nothing is written. */
    (void)hConsoleOutput;
    (void)lpBuffer;
    (void)nNumberOfCharsToWrite;
    (void)lpNumberOfCharsWritten;
    (void)lpReserved;
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!WriteConsoleW", "WriteConsoleW: there is no console");
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
