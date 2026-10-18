/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
 * RT-CASES:  flushes a file opened for writing; does not change last error on success; a handle opened for reading only fails with ERROR_ACCESS_DENIED; NULL and a handle that is not open fail with ERROR_INVALID_HANDLE
 * RT-TRAPS:  core: a character device; core: a directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, FlushFileBuffers, HANDLE hFile)
{
    /* The core does all of it (rt_file_flush, rt/file.h): the handle lookup, the fsync, and the
     * failures ERROR_INVALID_HANDLE and ERROR_ACCESS_DENIED, which the core sets itself.  Success
     * leaves last error alone.  A character device or a directory handle is a stop in the core:
     * no guard is added here. */
    return rt_file_flush(hFile);
}
