/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-leavecriticalsection
 * RT-CASES:  leaves a section held once: freed, owner cleared, wait word 0; leaves one of two holds: still owned, count down by one; a leave by a thread that does not own the section stops the run; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, LeaveCriticalSection, LPCRITICAL_SECTION lpCriticalSection)
{
    /* No return value.  The documentation calls a leave by a thread that does not own the
     * section an error that can leave other threads waiting for ever.  Windows does not
     * check it; the core stops the run instead (rt/lock.h). */
    rt_cs_leave(ctx, lpCriticalSection);
}
