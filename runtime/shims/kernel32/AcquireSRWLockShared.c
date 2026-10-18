/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-acquiresrwlockshared
 * RT-CASES:  a free lock is taken shared: another shared holder is allowed and an exclusive holder is refused; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, AcquireSRWLockShared, PSRWLOCK SRWLock)
{
    rt_srw_acquire_shared(SRWLock);
}
