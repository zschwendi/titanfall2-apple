/* RT-STATUS: advisory
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriorityboost
 * RT-CASES:  accepts the calling thread's pseudo-handle with TRUE and with FALSE; NULL and an event handle fail with ERROR_INVALID_HANDLE; success leaves last error alone; the log line appears once for two calls
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"
#include "rt/handle.h"

SHIM(kernel32, BOOL, SetThreadPriorityBoost, HANDLE hThread, BOOL bDisablePriorityBoost)
{
    /* Priority boosts are recorded by the runtime and otherwise ignored (docs/RUNTIME.md 6.3),
     * so bDisablePriorityBoost has no effect here.  The handle is still checked: the core
     * fails with ERROR_INVALID_HANDLE (set by the core) for a handle that is not an open thread,
     * and the reference it hands back is dropped at once. */
    RtObject *o;

    (void)bDisablePriorityBoost;
    if (!rt_thread_from_handle(hThread, &o)) return FALSE;
    rt_object_unref(o);
    rt_log_once(RT_CH_THREAD, RT_LL_DEBUG, "SetThreadPriorityBoost",
                "SetThreadPriorityBoost(...): accepted and ignored");
    return TRUE;
}
