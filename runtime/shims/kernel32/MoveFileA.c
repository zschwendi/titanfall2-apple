/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-movefilea
 * RT-CASES:  a file moves to a new name in the same directory and into a sub-directory; a directory moves; a target that exists fails with ERROR_ALREADY_EXISTS and both files stay unchanged; a source that does not exist fails with ERROR_FILE_NOT_FOUND; a target in a directory that does not exist fails with ERROR_PATH_NOT_FOUND; success leaves last error alone
 * RT-TRAPS:  core: lpNewFileName NULL
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, MoveFileA, LPCSTR lpExistingFileName, LPCSTR lpNewFileName)
{
    /* The core does all of it (rt_file_move, rt/file.h): it translates both names, refuses an
     * existing target with ERROR_ALREADY_EXISTS (a rename would replace it), moves the file or
     * directory, and sets last error on failure.  Success leaves last error alone.
     *
     * A NULL lpNewFileName is the delete-at-reboot form of MoveFileEx: the core stops on it
     * (rt_file_move, step 1), so the shim adds no guard for it; RT-TRAPS lists it. */
    return rt_file_move(RT_NAME_A(lpExistingFileName), RT_NAME_A(lpNewFileName), 0);
}
