/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-createdirectoryw
 * RT-CASES:  creates a directory the host can see; success leaves last error alone; a second create fails with ERROR_ALREADY_EXISTS; a create over a file fails with ERROR_ALREADY_EXISTS; a create under a parent that does not exist fails with ERROR_PATH_NOT_FOUND; a NULL name fails with ERROR_PATH_NOT_FOUND; lpSecurityAttributes is ignored, NULL or not
 * RT-TRAPS:  core: a colon in a path component; core: a DOS device name with an extension
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, CreateDirectoryW, LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    /* lpSecurityAttributes is not read, NULL or not: this runtime never creates a child
     * process, so bInheritHandle has no effect (planner's decision, 2026-10-09), and the host
     * directory gets the default mode.  Ignoring the pointer is not a trap. */
    (void)lpSecurityAttributes;

    /* The core does all of it (rt_dir_create, rt/file.h): translates the name, makes the
     * directory, drops the parent's cached listing, and sets last error on failure
     * (ERROR_ALREADY_EXISTS, ERROR_PATH_NOT_FOUND, also for a NULL name).  The name is passed
     * as it is, NULL included.  Success leaves last error alone. */
    return rt_dir_create(RT_NAME_W(lpPathName));
}
