/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-setendoffile
 * RT-CASES:  cuts the file at the position, which does not move; extends the file to a position beyond the end, which does not move the position; a handle that cannot write fails with ERROR_ACCESS_DENIED; a handle that is not open fails with ERROR_INVALID_HANDLE; success does not change last error
 * RT-TRAPS:  core: character device handle; core: directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, SetEndOfFile, HANDLE hFile)
{
    /* The core does all of it (rt_file_set_eof, rt/file.h): the file's size becomes the position,
     * the position does not move, and the failures set last error.  A character device or a
     * directory handle STOPS in the core: no guard here. */
    return rt_file_set_eof(hFile);
}
