/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-getprocessaffinitymask
 * RT-CASES:  both masks are the system mask at first; a mask stored by SetProcessAffinityMask is read back while the system mask stays; NULL and an event handle fail with ERROR_INVALID_HANDLE and write neither mask; success does not change last error
 * RT-TRAPS:  core: a NULL mask pointer
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, GetProcessAffinityMask, HANDLE hProcess, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask)
{
    /* The core does all of it (rt_process_get_affinity, rt/thread.h): it accepts (HANDLE)-1 and an
     * open process object, stores the process mask and the system mask, and fails with
     * ERROR_INVALID_HANDLE (set by the core) for any other handle.  Both outputs are written on
     * success only.  A NULL output pointer is fatal in the core ("a mask pointer is NULL"); the
     * shim adds no guard for it, and it is listed in RT-TRAPS. */
    return rt_process_get_affinity(hProcess, lpProcessAffinityMask, lpSystemAffinityMask);
}
