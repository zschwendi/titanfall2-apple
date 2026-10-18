/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-findnextfilew
 * RT-CASES:  each next entry in the core's order, with its attributes, times and size; after the last entry FALSE with ERROR_NO_MORE_FILES, every time, and the buffer untouched; a pattern with one match has no next entry; a NULL handle and a file handle fail with ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  core: lpFindFileData NULL
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, FindNextFileW, HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
{
    /* Nothing here: the core checks the handle and makes the stop for a NULL
     * lpFindFileData (rt_find_next_w, rt/file.h). */
    return rt_find_next_w(hFindFile, lpFindFileData);
}
