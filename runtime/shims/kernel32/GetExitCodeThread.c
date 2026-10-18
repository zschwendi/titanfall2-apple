/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodethread
 * RT-CASES:  a running thread gives STILL_ACTIVE and TRUE, and so does the pseudo-handle of the current thread; NULL and a handle that is not a thread fail with ERROR_INVALID_HANDLE and leave the DWORD unwritten; success does not change last error
 * RT-TRAPS:  core: lpExitCode NULL
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, GetExitCodeThread, HANDLE hThread, LPDWORD lpExitCode)
{
    /* The core does all of it (rt_thread_get_exit_code, rt/thread.h): STILL_ACTIVE until the
     * thread has exited, then its exit code; it accepts the pseudo-handle of the current thread;
     * it fails with ERROR_INVALID_HANDLE (set by the core) for a handle that is not an open thread.
     * The documentation's access right (THREAD_QUERY_INFORMATION on the handle) is not checked:
     * this runtime keeps no access rights on a handle (see OpenThread), so there is no case
     * for it to refuse.
     *
     * A NULL lpExitCode STOPS in the core (Windows raises an access violation): no guard here. */
    return rt_thread_get_exit_code(hThread, lpExitCode);
}
