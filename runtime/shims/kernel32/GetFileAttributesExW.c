/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfileattributesexw
 * RT-CASES:  a file of 5 bytes: attributes, size 5 and last write time; a directory: size 0; a level other than GetFileExInfoStandard fails with ERROR_INVALID_PARAMETER and leaves the 36 bytes; a missing file fails with ERROR_FILE_NOT_FOUND and leaves the 36 bytes; success leaves last error alone
 * RT-TRAPS:  core: lpFileInformation NULL; core: NUL; core: host object of another type
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, GetFileAttributesExW, LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
    /* The core does all of it (rt_file_get_attributes_ex, rt/file.h): the name is translated as
     * a W name, a level other than GetFileExInfoStandard fails with ERROR_INVALID_PARAMETER
     * (checked first), a missing file fails with the mapped stat error, and the 36 bytes are
     * filled only on success.  The core sets last error on failure and leaves it alone on
     * success.  A NULL buffer, NUL and a host object of another type stop in the core (RT-TRAPS). */
    return rt_file_get_attributes_ex(RT_NAME_W(lpFileName), (DWORD)fInfoLevelId, lpFileInformation);
}
