/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
 * RT-CASES:  waits for a signaled event and returns WAIT_OBJECT_0; zero timeout on a reset event returns WAIT_TIMEOUT at once; timeout of 20 ms on a reset event returns WAIT_TIMEOUT after 20 ms; each wait on a semaphore takes one count; a mutex taken twice by the same thread returns WAIT_OBJECT_0 both times; a handle that is not open fails with WAIT_FAILED and ERROR_INVALID_HANDLE; is not alertable, so a queued APC does not run; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, DWORD, WaitForSingleObject, HANDLE hHandle, DWORD dwMilliseconds)
{
    /* Never alertable: FALSE is passed, so a queued APC is not run by this wait.  INFINITE is
     * passed through: the core never times out. */
    return rt_wait_handle(ctx, hHandle, dwMilliseconds, FALSE);
}
