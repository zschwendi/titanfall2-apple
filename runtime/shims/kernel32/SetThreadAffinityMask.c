/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setthreadaffinitymask
 * RT-CASES:  returns the process mask until the thread sets its own, then the mask that was stored; after SetProcessAffinityMask every thread has the new process mask, one that set its own too; all bits set, or the low 32 bits all set, is all processors: returns the previous mask and stores the process mask; mask 0, and a mask with a bit the process does not have unless its low 32 bits are all set, fail with ERROR_INVALID_PARAMETER; NULL and an event handle fail with ERROR_INVALID_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, DWORD_PTR, SetThreadAffinityMask, HANDLE hThread, DWORD_PTR dwThreadAffinityMask)
{
    /* The core does all of it (rt_thread_set_affinity, rt/thread.h): it records the mask, returns
     * the one the thread had, and fails with ERROR_INVALID_HANDLE (not an open handle, or not a
     * thread) or ERROR_INVALID_PARAMETER (mask 0, or a bit outside the process mask unless the
     * low 32 bits are all set, which is all processors), setting last error itself.  Success
     * leaves last error alone.  Nothing is done to the host thread:
     * macOS on Apple silicon has no affinity.  A thread's mask follows SetProcessAffinityMask
     * (the core handles that: rt/thread.h).
     *
     * Not checked here: the documentation requires the handle to have THREAD_SET_(LIMITED_)
     * INFORMATION and THREAD_QUERY_(LIMITED_)INFORMATION access, and gives no error code for a
     * handle without them.  The core takes no access argument, and rt/thread.h says the desired
     * access of OpenThread is ignored, so the shim has nothing to check it against. */
    return rt_thread_set_affinity(hThread, dwThreadAffinityMask);
}
