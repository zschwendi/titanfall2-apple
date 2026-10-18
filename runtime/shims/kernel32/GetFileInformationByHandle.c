/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle
 * RT-CASES:  a file of 5 bytes: attributes, size 5, last write time, volume serial, one link and file index; a directory opened with FILE_FLAG_BACKUP_SEMANTICS: attributes FILE_ATTRIBUTE_DIRECTORY and size 0; a handle that is not open fails with ERROR_INVALID_HANDLE and leaves the 52 bytes; success leaves last error alone
 * RT-TRAPS:  core: lpFileInformation NULL
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, GetFileInformationByHandle, HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
{
    /* The core does all of it (rt_file_info_by_handle, rt/file.h): the attributes, times, size,
     * volume serial, link count and file index come from fstat, and a handle that is not open
     * fails with ERROR_INVALID_HANDLE.  The core sets last error on failure and leaves it alone
     * on success.  A NULL buffer stops in the core (RT-TRAPS); a character device stops there
     * too, but no test can reach one (see the reply). */
    return rt_file_info_by_handle(hFile, lpFileInformation);
}
