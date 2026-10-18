/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfileattributesa
 * RT-CASES:  a file with the owner's write bit is ARCHIVE; a file without the owner's write bit is ARCHIVE and READONLY; a directory is FILE_ATTRIBUTE_DIRECTORY; a missing name fails with ERROR_FILE_NOT_FOUND; a name in a directory that does not exist fails with ERROR_PATH_NOT_FOUND; a NULL name fails with ERROR_INVALID_PARAMETER; does not change last error on success
 * RT-TRAPS:  core: NUL; core: a host object of another type
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, DWORD, GetFileAttributesA, LPCSTR lpFileName)
{
    /* The core does all of it (rt_file_get_attributes, rt/file.h): it translates the name with
     * RT_PATH_EXISTING, stats it, computes the attributes, and sets last error when it fails.
     * Success leaves last error alone.  The name is passed as it came, NULL included; the shim
     * converts nothing and looks at no character of it.
     *
     * Not settled by the core: the documentation says that for a symbolic link the function
     * returns the attributes of the link itself, while the core follows the link (rt/file.h,
     * ATTRIBUTES).  The shim cannot see the link without touching the path, so it decides
     * nothing here. */
    return rt_file_get_attributes(RT_NAME_A(lpFileName));
}
