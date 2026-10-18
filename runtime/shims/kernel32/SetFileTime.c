/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-setfiletime
 * RT-CASES:  a write time of 2001-09-09 01:46:40 UTC (unix 1000000000) is stored and the access time stays; an access time alone; all three pointers NULL changes nothing and returns TRUE; a FILETIME of 0 leaves that time unchanged; a creation time is stored as the birth time, and a write time earlier than it does not undo it; a handle that is not open fails with ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  core: a FILETIME of 0xFFFFFFFF:0xFFFFFFFF; core: a time before 1970; core: a character device
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, SetFileTime, HANDLE hFile, const FILETIME *lpCreationTime, const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime)
{
    /* The core does all of it (rt_file_set_times, rt/file.h): a NULL pointer or a FILETIME of 0
     * leaves that time unchanged, the access and write times are set with futimens, the creation
     * time (the birth time) is then set with fsetattrlist, and the core sets last error on failure
     * and leaves it alone on success.  A FILETIME of 0xFFFFFFFF:0xFFFFFFFF, a time before 1970 and
     * a character device stop in the core (RT-TRAPS). */
    return rt_file_set_times(hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime);
}
