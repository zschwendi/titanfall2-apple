/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthread
 * RT-CASES:  returns the pseudo-handle for the current thread, all 64 bits set; returns the same value on every call; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, HANDLE, GetCurrentThread)
{
    /* The documented pseudo-handle is the constant (HANDLE)-2 (rt/win/base.h, section 4.1).
     * It always names the calling thread: the core resolves it per thread when it is used
     * (rt_handle_ref, DuplicateHandle).  Nothing is recorded here, and access rights are not
     * modelled, so none is described. */
    return RT_PSEUDO_THREAD;
}
