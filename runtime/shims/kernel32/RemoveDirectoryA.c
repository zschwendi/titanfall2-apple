/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-removedirectorya
 * RT-CASES:  removes an empty directory; a directory with a file in it is not removed and fails with ERROR_DIR_NOT_EMPTY; a name that does not exist fails with ERROR_PATH_NOT_FOUND; success leaves last error alone
 * RT-TRAPS:  core: RemoveDirectory on a file
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, RemoveDirectoryA, LPCSTR lpPathName)
{
    /* The core does all of it (rt_dir_remove, rt/file.h): translates the name, removes an
     * empty directory, drops the parent's cached listing, and sets last error on failure
     * (ERROR_DIR_NOT_EMPTY, ERROR_PATH_NOT_FOUND).  Success leaves last error alone.
     *
     * A missing name is ERROR_PATH_NOT_FOUND, the core's answer for every directory call.
     * Windows is believed to give ERROR_FILE_NOT_FOUND when only the last component is
     * missing (not verified): an open question on the core (rt_dir_remove), not decided here.
     *
     * A name that leads to a file is a stop in the core (rt_dir_remove), not a failure: the
     * shim adds no guard for it, and it is listed in RT-TRAPS as "core: RemoveDirectory on a
     * file". */
    return rt_dir_remove(RT_NAME_A(lpPathName));
}
