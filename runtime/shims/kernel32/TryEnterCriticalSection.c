/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-tryentercriticalsection
 * RT-CASES:  enters a free section and returns TRUE; enters a section the caller owns and returns TRUE, raising the recursion count; a section held by another thread returns FALSE and changes nothing; a section never initialised or deleted stops the run; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, BOOL, TryEnterCriticalSection, LPCRITICAL_SECTION lpCriticalSection)
{
    /* TRUE if the section was entered, or the caller owned it already; FALSE if another thread
     * holds it.  The core never waits here (rt/lock.h).  The documentation gives no error code
     * for FALSE, so none is set. */
    return rt_cs_try_enter(ctx, lpCriticalSection);
}
