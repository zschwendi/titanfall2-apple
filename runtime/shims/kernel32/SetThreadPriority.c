/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority
 * RT-CASES:  accepts the calling thread's pseudo-handle and stores each of the seven values, which GetThreadPriority reads back; NULL and an event handle fail with ERROR_INVALID_HANDLE and leave the priority as it was; does not change last error on success
 * RT-TRAPS:  core: a priority that is not one of the seven
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, SetThreadPriority, HANDLE hThread, int nPriority)
{
    /* The core does all of it (rt_thread_set_priority, rt/thread.h): it records the value in the
     * thread object, accepts the calling thread's pseudo-handle, and fails with
     * ERROR_INVALID_HANDLE (set by the core) for a handle that is not an open thread.  Success
     * leaves last error alone.
     *
     * A priority that is not one of the seven THREAD_PRIORITY_ values is fatal in the core,
     * whatever the handle.  That includes the values the documentation gives for other uses
     * (THREAD_MODE_BACKGROUND_BEGIN and THREAD_MODE_BACKGROUND_END, and -7 to -3 and 3 to 6 for
     * a realtime-class thread).  The shim adds no guard for them; the core's stop is listed in
     * RT-TRAPS. */
    return rt_thread_set_priority(hThread, nPriority);
}
