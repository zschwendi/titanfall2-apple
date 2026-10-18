/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-resetevent
 * RT-CASES:  set then reset: a later wait times out; resetting an event that is already reset changes nothing; an auto-reset event that is set is made unset; NULL, a closed handle and a handle of another kind fail with ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, BOOL, ResetEvent, HANDLE hEvent)
{
    /* The core does all of it (rt_event_reset, rt/sync.h): the event becomes reset and nobody is
     * woken.  It fails with ERROR_INVALID_HANDLE (set by the core) for a handle that is not open
     * and for a handle that is not an event.  Success leaves last error alone.
     *
     * As SetEvent: the documentation requires the EVENT_MODIFY_STATE access right on the handle,
     * and the runtime has no access checking (rt/sync.h), so the shim does not refuse a handle
     * for lacking it. */
    return rt_event_reset(ctx, hEvent);
}
