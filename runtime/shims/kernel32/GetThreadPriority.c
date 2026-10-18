/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadpriority
 * RT-CASES:  the calling thread's pseudo-handle starts at THREAD_PRIORITY_NORMAL; returns each of the seven values that SetThreadPriority stored; NULL and an event handle fail with THREAD_PRIORITY_ERROR_RETURN and ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, int, GetThreadPriority, HANDLE hThread)
{
    /* The core does all of it (rt_thread_get_priority, rt/thread.h): it returns the recorded
     * value, or THREAD_PRIORITY_ERROR_RETURN with ERROR_INVALID_HANDLE (set by the core) for a
     * handle that is not an open thread.  Success leaves last error alone.
     *
     * The documentation also lists -7 to -3 and 3 to 6 as results for a realtime-class thread.
     * The core records only the seven THREAD_PRIORITY_ values (SetThreadPriority stops on any
     * other), so this function never returns those. */
    return rt_thread_get_priority(hThread);
}
