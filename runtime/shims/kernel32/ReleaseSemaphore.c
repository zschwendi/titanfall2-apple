/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-releasesemaphore
 * RT-CASES:  release raises the count and stores the previous count; NULL previous count is accepted and the count is still raised; release count of 0 or less fails with ERROR_INVALID_PARAMETER and changes nothing; passing the maximum fails with ERROR_TOO_MANY_POSTS, count unchanged, previous count not written; a handle that is not a semaphore fails with ERROR_INVALID_HANDLE; NULL and a handle that is not open fail with ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, BOOL, ReleaseSemaphore, HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount)
{
    /* The core does all of it (rt_semaphore_release, rt/sync.h): it checks the release count
     * (ERROR_INVALID_PARAMETER), the maximum (ERROR_TOO_MANY_POSTS) and the handle
     * (ERROR_INVALID_HANDLE), wakes waiting threads in queue order, stores the previous count
     * on success only, and sets last error on failure.  Success leaves last error alone.
     *
     * The documentation says the handle must have SEMAPHORE_MODIFY_STATE and does not say what
     * happens without it.  rt/sync.h ignores the desired access of every sync object (no access
     * checking), so every semaphore handle has the right and that case is not modelled here.
     * It is raised for the planner. */
    return rt_semaphore_release(ctx, hSemaphore, lReleaseCount, lpPreviousCount);
}
