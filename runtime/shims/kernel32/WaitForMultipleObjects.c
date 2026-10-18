/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects
 * RT-CASES:  wait-any returns the lowest signalled index and takes only that object; wait-all returns WAIT_OBJECT_0 once every object is signalled and takes them all together, and times out with nothing taken until then; up to MAXIMUM_WAIT_OBJECTS objects; a count of 0 or above MAXIMUM_WAIT_OBJECTS fails with ERROR_INVALID_PARAMETER; wait-all with the same handle twice fails with ERROR_INVALID_PARAMETER and takes nothing; a handle that is not open fails with ERROR_INVALID_HANDLE; is not alertable, so a queued APC does not run; success does not change last error
 * RT-TRAPS:  core: NULL lpHandles
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, DWORD, WaitForMultipleObjects, DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
{
    /* Not alertable: a queued APC is never run by this call.  The core checks the arguments,
     * takes the objects, and sets last error itself on failure, so the result is returned
     * as it is. */
    return rt_wait_handles(ctx, nCount, lpHandles, bWaitAll, dwMilliseconds, FALSE);
}
