/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getdrivetypew
 * RT-CASES:  NULL root is DRIVE_FIXED; C root with a backslash is DRIVE_FIXED in either letter case; C root with a forward slash is DRIVE_FIXED; D and Z roots with a backslash are DRIVE_NO_ROOT_DIR; C: with no slash is DRIVE_FIXED and D: with no slash is DRIVE_NO_ROOT_DIR; C, c, C?, c?, ? and the empty root are DRIVE_NO_ROOT_DIR; does not change last error
 * RT-TRAPS:  core: a root other than a drive letter and a colon (with or without a slash), C, C?, ? and the empty root
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, UINT, GetDriveTypeW, LPCWSTR lpRootPathName)
{
    /* The core reads the root's text only and never sets last error; the name goes to it
     * as it came, NULL included. */
    return rt_drive_type(RT_NAME_W(lpRootPathName));
}
