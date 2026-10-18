/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-waitforsingleobjectex
 * RT-CASES:  waits for a signaled event and returns WAIT_OBJECT_0; zero timeout on a reset event returns WAIT_TIMEOUT at once; timeout of 20 ms on a reset event returns WAIT_TIMEOUT after 20 ms; each wait on a semaphore takes one count; a mutex taken twice by the same thread returns WAIT_OBJECT_0 both times; a handle that is not open fails with WAIT_FAILED and ERROR_INVALID_HANDLE; an alertable wait with a queued APC returns WAIT_IO_COMPLETION and runs the APC; a non-alertable wait leaves a queued APC pending; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, DWORD, WaitForSingleObjectEx, HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable)
{
    /* bAlertable is passed on as given: nonzero lets a queued APC end the wait with
     * WAIT_IO_COMPLETION, as the core documents.  INFINITE is passed through: the core never
     * times out. */
    return rt_wait_handle(ctx, hHandle, dwMilliseconds, bAlertable);
}
