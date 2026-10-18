/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-findclose
 * RT-CASES:  closes a find handle, so the next FindNextFile and the next FindClose fail with ERROR_INVALID_HANDLE; NULL, INVALID_HANDLE_VALUE and a file handle fail with ERROR_INVALID_HANDLE, and the file handle still works afterwards; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, FindClose, HANDLE hFindFile)
{
    /* The core does all of it (rt_find_close, rt/file.h): it closes a handle of kind RT_K_FIND
     * and no other, and fails with ERROR_INVALID_HANDLE (set by the core) for NULL, for
     * INVALID_HANDLE_VALUE, for a handle that is not open and for a handle of any other kind,
     * such as a file handle.  Success leaves last error alone.  The documentation names no
     * error code for an invalid handle; ERROR_INVALID_HANDLE is the core's choice, as it is
     * for CloseHandle. */
    return rt_find_close(hFindFile);
}
