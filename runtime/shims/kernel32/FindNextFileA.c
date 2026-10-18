/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-findnextfilea
 * RT-CASES:  the entries come in turn, then FALSE with ERROR_NO_MORE_FILES every time and the buffer untouched; NULL, a file handle and a closed find handle fail with ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  core: lpFindFileData NULL; core: an entry name outside code page 1252
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, FindNextFileA, HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
    /* The core does all of it (rt_find_next_a, rt/file.h): the next entry, ERROR_NO_MORE_FILES
     * after the last, ERROR_INVALID_HANDLE for a handle that is not an open find handle, and
     * the stops.  Last error is set by the core on failure only. */
    return rt_find_next_a(hFindFile, lpFindFileData);
}
