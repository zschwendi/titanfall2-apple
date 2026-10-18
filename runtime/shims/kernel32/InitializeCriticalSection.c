/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-initializecriticalsection
 * RT-CASES:  initialises a free section, with every field set whatever the memory held before; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, InitializeCriticalSection, LPCRITICAL_SECTION lpCriticalSection)
{
    /* No return value.  Windows XP and Server 2003 could raise STATUS_NO_MEMORY here; from
     * Windows Vista on the call always succeeds, and the runtime allocates nothing.  The
     * documentation does not give the spin count of a section made by this call; the core's
     * initialiser stores 0 (rt/lock.h). */
    rt_cs_init(lpCriticalSection, 0);
}
