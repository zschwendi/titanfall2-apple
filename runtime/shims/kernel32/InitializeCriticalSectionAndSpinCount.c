/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionandspincount
 * RT-CASES:  initialises a free section with the spin count, whatever the memory held before; keeps the low 24 bits of the spin count and drops the high byte; returns TRUE for every spin count; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, BOOL, InitializeCriticalSectionAndSpinCount, LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount)
{
    /* The documentation says this call always succeeds from Windows Vista on, even in low
     * memory, so it always returns TRUE.  Windows XP and Server 2003 used the high bit of
     * dwSpinCount to ask for the section's event to be created at once; that bit has no
     * meaning here and is ignored.  The spin count is advisory: any value is accepted, and the
     * core keeps its low 24 bits (rt/lock.h). */
    rt_cs_init(lpCriticalSection, dwSpinCount);
    return TRUE;
}
