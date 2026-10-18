/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/handleapi/nf-handleapi-closehandle
 * RT-CASES:  closes a handle, so the next use and the next close fail; the object is destroyed with its last reference and not before; a pseudo-handle is accepted and has no effect; NULL and a handle that is not open fail with ERROR_INVALID_HANDLE; a find-file handle fails with ERROR_INVALID_HANDLE and stays open; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/handle.h"

SHIM(kernel32, BOOL, CloseHandle, HANDLE hObject)
{
    /* The core does all of it (rt_handle_close, rt/handle.h): it empties the slot and drops its
     * reference, accepts a pseudo-handle with no effect, and fails with ERROR_INVALID_HANDLE
     * (set by the core) for a handle that is not open and for a kind that has its own close
     * function (find-file handles: FindClose).  Success leaves last error alone.
     *
     * The documentation's debugger case (an exception for an invalid or pseudo handle when a
     * debugger is attached) does not arise: IsDebuggerPresent is FALSE and the guest is told
     * no debugger is attached (docs/RUNTIME.md section 9.4).
     *
     * Not modelled yet: the documentation says CloseHandle does not close a registry key
     * handle and returns no error for it.  The core closes such a handle like any other, so
     * this must be revisited when the registry unit lands. */
    return rt_handle_close(hObject);
}
