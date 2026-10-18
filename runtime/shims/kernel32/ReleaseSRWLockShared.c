/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-releasesrwlockshared
 * RT-CASES:  releasing one of two shared holders keeps the lock shared; releasing the last shared holder frees the lock; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, ReleaseSRWLockShared, PSRWLOCK SRWLock)
{
    rt_srw_release_shared(SRWLock);
}
