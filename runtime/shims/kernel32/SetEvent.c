/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-setevent
 * RT-CASES:  a manual-reset event stays set after SetEvent; setting an event that is already set changes nothing; an auto-reset event stays set until one wait consumes it; NULL, a closed handle and a handle of another kind fail with ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, BOOL, SetEvent, HANDLE hEvent)
{
    /* The core does all of it (rt_event_set, rt/sync.h): it sets the event, releases the waiters
     * a set would release (manual-reset: all that can be released; auto-reset: one, and the event
     * is left reset), and fails with ERROR_INVALID_HANDLE (set by the core) for a handle that is
     * not open and for a handle that is not an event.  Success leaves last error alone.
     *
     * The documentation requires the EVENT_MODIFY_STATE access right on the handle.  The runtime
     * has no access checking (rt/sync.h: the desired access is ignored), and the shim cannot see
     * an access mask, so a handle is never refused for lacking that right. */
    return rt_event_set(ctx, hEvent);
}
