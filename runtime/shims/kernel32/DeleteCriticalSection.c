/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-deletecriticalsection
 * RT-CASES:  zeroes all 40 bytes of a free section and logs nothing; zeroes a held section and logs that it was held; a deleted section can be initialised again; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, DeleteCriticalSection, LPCRITICAL_SECTION lpCriticalSection)
{
    /* No return value.  The documentation makes the caller responsible for the section being
     * unowned and not in use by another thread.  If it is held, the core logs that and zeroes
     * the structure all the same (rt/lock.h). */
    rt_cs_delete(lpCriticalSection);
}
