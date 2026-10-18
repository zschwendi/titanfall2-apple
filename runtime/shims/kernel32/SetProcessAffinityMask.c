/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setprocessaffinitymask
 * RT-CASES:  a mask of bit 0 alone is stored and read back while the system mask stays; a mask of 0 or with a bit outside the system mask fails with ERROR_INVALID_PARAMETER and leaves the stored mask unchanged; NULL and an event handle fail with ERROR_INVALID_HANDLE; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, SetProcessAffinityMask, HANDLE hProcess, DWORD_PTR dwProcessAffinityMask)
{
    /* The core does all of it (rt_process_set_affinity, rt/thread.h): it accepts (HANDLE)-1 and an
     * open process object, records the mask when it is not 0 and has no bit outside the system
     * mask, and fails with ERROR_INVALID_PARAMETER (set by the core) otherwise.  A handle that is
     * neither fails with ERROR_INVALID_HANDLE (set by the core).  Success leaves last error alone.
     * The mask is recorded only: no host thread is changed (macOS has no affinity). */
    return rt_process_set_affinity(hProcess, dwProcessAffinityMask);
}
