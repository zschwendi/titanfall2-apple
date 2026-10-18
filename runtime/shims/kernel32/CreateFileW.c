/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-createfilew
 * RT-CASES:  CREATE_NEW makes a file and fails the second time with ERROR_FILE_EXISTS; OPEN_EXISTING on a missing file fails with ERROR_FILE_NOT_FOUND and returns INVALID_HANDLE_VALUE; TRUNCATE_EXISTING on a missing file fails with ERROR_FILE_NOT_FOUND; CREATE_ALWAYS and OPEN_ALWAYS report an existing file through ERROR_ALREADY_EXISTS; last error is written on success and on failure; A and W open the same file; a NULL name fails with ERROR_INVALID_PARAMETER; an invalid disposition fails with ERROR_INVALID_PARAMETER; TRUNCATE_EXISTING without write access fails with ERROR_INVALID_PARAMETER; a directory opens only with FILE_FLAG_BACKUP_SEMANTICS, otherwise ERROR_ACCESS_DENIED; NUL opens as a device; DELETE_ON_CLOSE removes the name when the handle closes; a read-only file cannot be opened for writing: ERROR_ACCESS_DENIED; a sharing violation is logged and not enforced
 * RT-TRAPS:  core: template file; core: share mode; core: access rights; core: append-only handle; core: flags and attributes; core: CREATE_NEW on NUL
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, HANDLE, CreateFileW, LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    /* The core does all of it (rt_file_create, rt/file.h): the stops, the disposition and
     * name checks, the host open, the flags, the sharing log and last error, on success and
     * on failure.  This shim converts nothing and touches no path: the name goes to the core
     * as the guest gave it.
     *
     * lpSecurityAttributes is not used, NULL or not: the runtime never starts a child process,
     * so nothing can inherit the handle, and no security descriptor is modelled. */
    (void)lpSecurityAttributes;
    return rt_file_create(RT_NAME_W(lpFileName), dwDesiredAccess, dwShareMode,
                          dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}
