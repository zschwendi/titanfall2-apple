/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess
 * RT-CASES:  (HANDLE)-1 ends the process with the code as its exit status; a code above 254 exits with status 255; NULL, a handle that is not open and an event handle fail with ERROR_INVALID_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, BOOL, TerminateProcess, HANDLE hProcess, UINT uExitCode)
{
    /* The core does all of it (rt_process_terminate, rt/crash.h): the pseudo-handle of the
     * current process runs the exit sequence and does not return; any other handle must name
     * the current process' object, else the core fails with ERROR_INVALID_HANDLE (set by the
     * core).  Nothing here: the core sets last error itself and this shim sets none. */
    return rt_process_terminate(ctx, hProcess, uExitCode);
}
