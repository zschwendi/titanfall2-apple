/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/handleapi/nf-handleapi-duplicatehandle
 * RT-CASES:  duplicates a handle to the same object, and each handle closes on its own; DUPLICATE_CLOSE_SOURCE closes the source and keeps the duplicate; a NULL lpTargetHandle duplicates without returning the handle; a NULL hTargetProcessHandle with DUPLICATE_CLOSE_SOURCE only closes the source; an invalid source handle fails with ERROR_INVALID_HANDLE; dwDesiredAccess is ignored, as access rights are not modelled; does not change last error on success
 * RT-TRAPS:  hSourceProcessHandle other than the current process; hTargetProcessHandle other than the current process; hTargetProcessHandle NULL without DUPLICATE_CLOSE_SOURCE; pseudo-handle as hSourceHandle; dwOptions bits other than; bInheritHandle TRUE
 */
#include "rt/shim.h"
#include "rt/handle.h"

#define RT_DUP_OPTIONS_KNOWN  (DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS)

SHIM(kernel32, BOOL, DuplicateHandle, HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle, LPHANDLE lpTargetHandle, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions)
{
    /* The process arguments: only this process can be named, by its pseudo-handle, because the
     * runtime has no other process objects.  Every other value is refused before anything is
     * done, and so is every option the documentation does not list. */
    if (hSourceProcessHandle != RT_PSEUDO_PROCESS)
        RT_UNIMPLEMENTED(ctx, "hSourceProcessHandle other than the current process: %p", hSourceProcessHandle);
    if (dwOptions & ~RT_DUP_OPTIONS_KNOWN)
        RT_UNIMPLEMENTED(ctx, "dwOptions bits other than DUPLICATE_CLOSE_SOURCE and DUPLICATE_SAME_ACCESS: %#x",
                         (unsigned)(dwOptions & ~RT_DUP_OPTIONS_KNOWN));
    /* The documentation allows a NULL target process only with DUPLICATE_CLOSE_SOURCE. */
    if (hTargetProcessHandle == NULL && !(dwOptions & DUPLICATE_CLOSE_SOURCE))
        RT_UNIMPLEMENTED(ctx, "hTargetProcessHandle NULL without DUPLICATE_CLOSE_SOURCE: the documentation allows NULL only to close the source");
    if (hTargetProcessHandle != NULL && hTargetProcessHandle != RT_PSEUDO_PROCESS)
        RT_UNIMPLEMENTED(ctx, "hTargetProcessHandle other than the current process: %p", hTargetProcessHandle);
    /* The core's pseudo-handle hooks (the process object, RtThread.sub[RT_SUB_HANDLE]) are not
     * registered yet, so a pseudo-handle source cannot be duplicated: refused here, not left to
     * the core's fatal report. */
    if (hSourceHandle == RT_PSEUDO_PROCESS || hSourceHandle == RT_PSEUDO_THREAD)
        RT_UNIMPLEMENTED(ctx, "pseudo-handle as hSourceHandle (%p): the process and thread objects are not registered with the handle table yet", hSourceHandle);

    if (hTargetProcessHandle == NULL) {
        /* The documentation: with a NULL target process, DUPLICATE_CLOSE_SOURCE closes the source
         * and nothing is duplicated; lpTargetHandle, dwDesiredAccess and bInheritHandle are
         * ignored.  The close fails as CloseHandle does (ERROR_INVALID_HANDLE for a handle that is
         * not open).  UNSURE: the documentation says the source is closed regardless of error
         * status but does not name the error code for an invalid source. */
        return rt_handle_close(hSourceHandle);
    }

    /* Inheritance is not modelled: the core keeps no inherit flag and the runtime starts no
     * child process, so a duplicate that a child could inherit cannot be represented. */
    if (bInheritHandle)
        RT_UNIMPLEMENTED(ctx, "bInheritHandle TRUE: a duplicate that a child process can inherit is not modelled");

    /* Access rights are not modelled: every handle has the access of its object.  The
     * documentation's DUPLICATE_SAME_ACCESS makes dwDesiredAccess ignored, and without that flag
     * it is ignored too, so the duplicate has no access of its own choosing.  That is a known
     * gap, not a guess about Windows. */
    (void)dwDesiredAccess;

    HANDLE dup = rt_handle_dup(hSourceHandle);   /* a new slot for the same object, or NULL with the error set */
    if (dwOptions & DUPLICATE_CLOSE_SOURCE) {
        /* The documentation closes the source regardless of the error status.  rt_handle_dup has
         * just looked the source up, so this close fails only if another thread closed it in
         * between; then the duplicate still stands.  That race is not modelled further. */
        rt_handle_close(hSourceHandle);
    }
    if (dup == NULL) return FALSE;                /* the core set the last error (ERROR_INVALID_HANDLE) */

    /* lpTargetHandle is written only on success: the documentation does not say what it holds
     * after a failure.  NULL duplicates without returning the handle; the documentation calls
     * that a backward-compatibility behaviour which loses resources until the process ends, and
     * the duplicate is kept as it is there. */
    if (lpTargetHandle != NULL) *lpTargetHandle = dup;
    return TRUE;
}
