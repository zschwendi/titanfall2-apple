/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-initializesrwlock
 * RT-CASES:  the lock is unlocked whatever its bytes were before; the lock can be acquired at once afterwards; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, InitializeSRWLock, PSRWLOCK SRWLock)
{
    rt_srw_init(SRWLock);
}
