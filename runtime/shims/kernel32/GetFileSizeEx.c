/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfilesizeex
 * RT-CASES:  reports the size of a 10-byte file and of an empty file; reports a size above 4 GiB and the size 0xFFFFFFFF in full; a handle that is not open fails with ERROR_INVALID_HANDLE; success does not change last error
 * RT-TRAPS:  core: lpFileSize NULL; core: character device handle; core: directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, GetFileSizeEx, HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
    /* The core does all of it (rt_file_size, rt/file.h): the size from fstat, last error set on
     * failure and left alone on success.  A NULL size and a character device or a directory
     * handle STOP in the core: no guard here. */
    return rt_file_size(hFile, lpFileSize ? &lpFileSize->QuadPart : NULL);
}
