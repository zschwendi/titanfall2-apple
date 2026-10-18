/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess
 * RT-CASES:  (HANDLE)-1 gives TRUE and STILL_ACTIVE, and leaves last error alone; NULL and an event handle fail with ERROR_INVALID_HANDLE and do not write the DWORD
 * RT-TRAPS:  core: lpExitCode NULL
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, BOOL, GetExitCodeProcess, HANDLE hProcess, LPDWORD lpExitCode)
{
    /* The core does all of it (rt_process_exit_code, rt/crash.h): the pseudo-handle of the
     * current process gives STILL_ACTIVE, any other handle must name the current process'
     * object, else the core fails with ERROR_INVALID_HANDLE (set by the core) and writes
     * nothing.  A NULL lpExitCode with a valid handle is a stop of the core (RT_UNIMPLEMENTED):
     * no guard here, so that the core's own checks run first. */
    return rt_process_exit_code(ctx, hProcess, lpExitCode);
}
