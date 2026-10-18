/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocess
 * RT-CASES:  returns the pseudo-handle for the current process, all 64 bits set; returns the same value on every call; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, HANDLE, GetCurrentProcess)
{
    /* The documented pseudo-handle is the constant (HANDLE)-1 (rt/win/base.h, section 4.1).
     * Nothing is recorded: CloseHandle and DuplicateHandle recognise the constant by its
     * 64-bit value.  Access rights are not modelled, so none is described here. */
    return RT_PSEUDO_PROCESS;
}
