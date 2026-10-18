/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-deletefilew
 * RT-CASES:  deletes a file, so the host no longer has it and a second delete fails with ERROR_FILE_NOT_FOUND; a read-only file (host mode 0444) stays and fails with ERROR_ACCESS_DENIED; a directory stays and fails with ERROR_ACCESS_DENIED; a name in a directory that does not exist fails with ERROR_PATH_NOT_FOUND; a NULL name fails with ERROR_PATH_NOT_FOUND; a file open with FILE_SHARE_DELETE can be deleted, and its handle stays open; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, DeleteFileW, LPCWSTR lpFileName)
{
    /* The core does all of it (rt_file_delete, rt/file.h): it translates the name, refuses a
     * directory and a read-only file with ERROR_ACCESS_DENIED, reports a missing file as
     * ERROR_FILE_NOT_FOUND, and sets last error itself on failure (leaving it alone on success).
     * A NULL name is passed on unchanged; the core answers it.
     *
     * Two places where the documentation and the core differ, not modelled here (the core is
     * the specification of this shim):
     *   1. A file that another handle has open without FILE_SHARE_DELETE: the documentation says
     *      the delete fails with ERROR_SHARING_VIOLATION.  The core logs "sharing violation not
     *      enforced" and deletes.
     *   2. A file that has a handle open: the documentation says the delete is marked and the
     *      file goes when the last handle closes (CreateFile of the name fails with
     *      ERROR_ACCESS_DENIED until then).  The core unlinks it at once, as POSIX delete does,
     *      and its open handles stay readable. */
    return rt_file_delete(RT_NAME_W(lpFileName));
}
