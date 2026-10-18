/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-openthread
 * RT-CASES:  opens the running thread with the given id, and success does not change last error; an id no running thread has fails with NULL and ERROR_INVALID_PARAMETER
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, HANDLE, OpenThread, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId)
{
    /* The core does all of it (rt_thread_open, rt/thread.h): a new handle to the running guest
     * thread with this id, or NULL with ERROR_INVALID_PARAMETER (no running thread has the id)
     * or ERROR_NOT_ENOUGH_MEMORY (the handle table is full), both set by the core.  Success
     * leaves last error alone.
     *
     * dwDesiredAccess and bInheritHandle are ignored: this runtime has no access checks (the
     * caller is not refused any right) and no child processes, so nothing inherits a handle.
     *
     * Known difference from Windows, in the core: Windows also opens a thread that has ended
     * while a handle to it is still open; the core fails for it with ERROR_INVALID_PARAMETER and
     * logs that once at warn level. */
    (void)dwDesiredAccess;
    (void)bInheritHandle;
    return rt_thread_open(dwThreadId);
}
