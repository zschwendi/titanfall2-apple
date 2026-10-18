/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
 * RT-CASES:  (HANDLE)-1 gives TRUE, a creation time, kernel and user times, an exit time of 0, and leaves last error alone; a second call gives the same creation time, and kernel and user times not less than before; an event handle gives FALSE with ERROR_INVALID_HANDLE and writes nothing
 * RT-TRAPS:  lpCreationTime NULL; lpExitTime NULL; lpKernelTime NULL; lpUserTime NULL
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, GetProcessTimes, HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime, LPFILETIME lpUserTime)
{
    /* The page does not say what a NULL pointer does: each one is a stop of the shim. */
    if (!lpCreationTime) RT_UNIMPLEMENTED(ctx, "GetProcessTimes with lpCreationTime NULL");
    if (!lpExitTime) RT_UNIMPLEMENTED(ctx, "GetProcessTimes with lpExitTime NULL");
    if (!lpKernelTime) RT_UNIMPLEMENTED(ctx, "GetProcessTimes with lpKernelTime NULL");
    if (!lpUserTime) RT_UNIMPLEMENTED(ctx, "GetProcessTimes with lpUserTime NULL");

    /* The core sets ERROR_INVALID_HANDLE and writes nothing when the handle is wrong. */
    if (!rt_process_times(hProcess, lpCreationTime, lpKernelTime, lpUserTime)) return FALSE;

    /* The exit time is undefined for a process that has not exited: it is stored as 0. */
    lpExitTime->dwLowDateTime = 0;
    lpExitTime->dwHighDateTime = 0;
    return TRUE;
}
