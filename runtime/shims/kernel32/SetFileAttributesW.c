/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-setfileattributesw
 * RT-CASES:  READONLY and then NORMAL on a file, seen through rt_file_get_attributes and the host mode; a missing file fails with ERROR_FILE_NOT_FOUND; a NULL name fails with the code rt_file_get_attributes gives; a directory given FILE_ATTRIBUTE_DIRECTORY, as GetFileAttributesW returns it, is left alone and returns TRUE; success leaves last error alone
 * RT-TRAPS:  core: an attribute bit that cannot be set; core: host object of another type
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, SetFileAttributesW, LPCWSTR lpFileName, DWORD dwFileAttributes)
{
    /* The core does all of it (rt_file_set_attributes, rt/file.h): an attribute bit outside
     * the settable set is checked first and stops, the name is translated as a W name, READONLY
     * alone changes the host mode, and a directory is left alone, FILE_ATTRIBUTE_DIRECTORY
     * included (it is ignored for one).  FILE_ATTRIBUTE_DIRECTORY on any other name stops.  The
     * core sets last error on failure and leaves it alone on success.  An attribute bit that
     * cannot be set (FILE_ATTRIBUTE_OFFLINE, for one), FILE_ATTRIBUTE_DIRECTORY on a name that is
     * not a directory, and a host object of another type stop in the core (RT-TRAPS). */
    return rt_file_set_attributes(RT_NAME_W(lpFileName), dwFileAttributes);
}
