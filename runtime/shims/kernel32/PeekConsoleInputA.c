/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/console/peekconsoleinput
 * RT-CASES:  fails for a handle that is not a console; fails with every pointer NULL; leaves a preset buffer and count alone; sets ERROR_INVALID_HANDLE; logs that there is no console
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, PeekConsoleInputA, HANDLE hConsoleInput, LPVOID lpBuffer, DWORD nLength, LPDWORD lpNumberOfEventsRead)
{
    /* This runtime has no console, so the call fails for every handle, as Windows fails for a
     * handle that is not a console's when the process has no console (the file contract's
     * decision). No argument is looked at, and nothing is written through lpBuffer or
     * lpNumberOfEventsRead. */
    (void)hConsoleInput;
    (void)lpBuffer;
    (void)nLength;
    (void)lpNumberOfEventsRead;
    rt_log_once(RT_CH_FILE, RT_LL_DEBUG, "kernel32.dll!PeekConsoleInputA", "PeekConsoleInputA: there is no console");
    return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
}
