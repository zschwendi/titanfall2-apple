/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-resumethread
 * RT-CASES:  resume of a suspended thread returns 1 and starts it; resume of a running thread returns 0 and does nothing; resume of a thread that has ended returns 0 and does nothing; the pseudo-handle names the calling thread and gives 0; NULL, an event and a closed thread handle fail with (DWORD)-1 and ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, DWORD, ResumeThread, HANDLE hThread)
{
    /* The core does all of it (rt_thread_resume, rt/thread.h): it returns the previous suspend
     * count, accepts the pseudo-handle, and fails with (DWORD)-1 and ERROR_INVALID_HANDLE (set
     * by the core) for a handle that is not an open thread.  A thread that has ended gives 0 and
     * nothing happens.  Last error is left alone on success.
     *
     * Not checked by the core or here: the documentation requires the THREAD_SUSPEND_RESUME
     * access right on the handle, and a thread handle carries no access rights in this runtime.
     * Every thread handle it makes has every right: CreateThread, the pseudo-handle, and
     * rt_thread_open (OpenThread), which takes no access argument.  So nothing reaches this call
     * without the right today.  The OpenThread unit must model the access right before it
     * accepts one. */
    return rt_thread_resume(hThread);
}
