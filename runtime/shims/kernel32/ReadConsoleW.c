/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/readconsole
 * RT-CASES:  fails with ERROR_INVALID_HANDLE when there is no console; writes neither the count nor the buffer; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/log.h"

SHIM(kernel32, BOOL, ReadConsoleW, HANDLE hConsoleInput, LPVOID lpBuffer, DWORD nNumberOfCharsToRead, LPDWORD lpNumberOfCharsRead, LPVOID pInputControl)
{
    /* This runtime has no console (docs/RUNTIME.md 7.3): the call fails as Windows fails when
     * the process has no console. No argument is looked at and nothing is written. */
    (void)hConsoleInput;
    (void)lpBuffer;
    (void)nNumberOfCharsToRead;
    (void)lpNumberOfCharsRead;
    (void)pInputControl;
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!ReadConsoleW", "ReadConsoleW: there is no console");
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
