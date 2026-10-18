/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-setfileattributesa
 * RT-CASES:  FILE_ATTRIBUTE_READONLY removes every write bit of the host mode; FILE_ATTRIBUTE_NORMAL clears READONLY and adds only the owner's write bit; a directory is unchanged and succeeds; a missing name fails with ERROR_FILE_NOT_FOUND; a name in a directory that does not exist fails with ERROR_PATH_NOT_FOUND; a NULL name fails with ERROR_INVALID_PARAMETER; does not change last error on success
 * RT-TRAPS:  core: an attribute bit that cannot be set
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, SetFileAttributesA, LPCSTR lpFileName, DWORD dwFileAttributes)
{
    /* The core does all of it (rt_file_set_attributes, rt/file.h): it stops first on a bit
     * outside its list, translates the name with RT_PATH_EXISTING, changes the host mode of a
     * regular file as its comment says, changes nothing for a directory, and sets last error
     * when it fails.  Success leaves last error alone.  The name is passed as it came, NULL
     * included; the shim converts nothing and looks at no character of it.
     *
     * Not matching the documentation, reported to the planner and not worked around here:
     *  - HIDDEN, SYSTEM, TEMPORARY and NOT_CONTENT_INDEXED are accepted and not stored
     *    (rt/file.h; docs/RUNTIME.md section 7.3): the call succeeds and nothing the guest can
     *    read changes;
     *  - FILE_ATTRIBUTE_OFFLINE is listed by the documentation as settable; the core stops on it. */
    return rt_file_set_attributes(RT_NAME_A(lpFileName), dwFileAttributes);
}
