/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfilesize
 * RT-CASES:  returns the size of a 10-byte file, with high part 0; returns 0 for an empty file; returns the low part and stores the high part of a size above 4 GiB; returns the low part of a size above 4 GiB with a NULL lpFileSizeHigh, and ERROR_SUCCESS for a low part of 0xFFFFFFFF; a size of 0xFFFFFFFF bytes returns INVALID_FILE_SIZE and sets last error to ERROR_SUCCESS; a handle that is not open fails with INVALID_FILE_SIZE and ERROR_INVALID_HANDLE and stores no high part; success does not change last error
 * RT-TRAPS:  core: character device handle; core: directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, DWORD, GetFileSize, HANDLE hFile, LPDWORD lpFileSizeHigh)
{
    LONGLONG size;

    /* The core sets last error when it fails.  The documentation then returns INVALID_FILE_SIZE,
     * and the high part is not stored. */
    if (!rt_file_size(hFile, &size)) return INVALID_FILE_SIZE;

    if (lpFileSizeHigh) *lpFileSizeHigh = (DWORD)(size >> 32);

    /* INVALID_FILE_SIZE is also a size: the documentation says GetLastError then gives NO_ERROR. */
    if ((DWORD)size == INVALID_FILE_SIZE) rt_set_last_error(ctx, ERROR_SUCCESS);
    return (DWORD)size;
}
