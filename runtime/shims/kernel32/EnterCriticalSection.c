/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-entercriticalsection
 * RT-CASES:  enters a free section, setting owner, recursion count and wait word; enters a section the caller owns again, raising the recursion count; a section never initialised or deleted stops the run; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, EnterCriticalSection, LPCRITICAL_SECTION lpCriticalSection)
{
    /* No return value.  Two things Windows does while a wait is blocked are not done here, and
     * the shim has no branch to trap on because the wait happens inside the core: Windows
     * raises EXCEPTION_POSSIBLE_DEADLOCK when a wait exceeds the CriticalSectionTimeout
     * registry value, and it ends the process instead of blocking while the process is
     * exiting.  The core waits without a timeout (rt/lock.h).  Planner to decide. */
    rt_cs_enter(ctx, lpCriticalSection);
}
