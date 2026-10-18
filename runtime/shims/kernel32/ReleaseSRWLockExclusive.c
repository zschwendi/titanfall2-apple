/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-releasesrwlockexclusive
 * RT-CASES:  releasing an exclusive lock frees it for the next holder; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, void, ReleaseSRWLockExclusive, PSRWLOCK SRWLock)
{
    rt_srw_release_exclusive(SRWLock);
}
