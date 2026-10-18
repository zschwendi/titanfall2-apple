/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-releasemutex
 * RT-CASES:  releases an owned mutex, once for each time it was owned; a release of an unowned mutex fails with ERROR_NOT_OWNER and changes nothing; NULL, a handle that is not open, and a handle of another kind fail with ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, BOOL, ReleaseMutex, HANDLE hMutex)
{
    /* The core does all of it (rt_mutex_release, rt/sync.h): it lowers the calling thread's
     * recursion depth, hands the mutex to the next waiter when the depth reaches zero, and sets
     * ERROR_NOT_OWNER or ERROR_INVALID_HANDLE on failure.  Success leaves last error alone. */
    return rt_mutex_release(ctx, hMutex);
}
