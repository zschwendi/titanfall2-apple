/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-acquiresrwlockexclusive
 * RT-CASES:  a free lock is taken exclusively, so no other holder can take it; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, AcquireSRWLockExclusive, PSRWLOCK SRWLock)
{
    rt_srw_acquire_exclusive(SRWLock);
}
