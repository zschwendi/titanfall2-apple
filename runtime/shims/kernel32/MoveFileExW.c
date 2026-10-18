/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-movefileexw
 * RT-CASES:  a file moves to a new name; a target that exists fails with ERROR_ALREADY_EXISTS without MOVEFILE_REPLACE_EXISTING; a target that exists is replaced with MOVEFILE_REPLACE_EXISTING; a source that does not exist fails with ERROR_FILE_NOT_FOUND; a target in a directory that does not exist fails with ERROR_PATH_NOT_FOUND; MOVEFILE_WRITE_THROUGH is accepted; success leaves last error alone
 * RT-TRAPS:  core: lpNewFileName NULL; core: a flag outside MOVEFILE_REPLACE_EXISTING, MOVEFILE_COPY_ALLOWED and MOVEFILE_WRITE_THROUGH; core: a move between volumes with MOVEFILE_COPY_ALLOWED; core: read-only target with MOVEFILE_REPLACE_EXISTING
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, MoveFileExW, LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags)
{
    /* The core does all of it (rt_file_move, rt/file.h): it refuses flags it does not build and
     * the delete-at-reboot form, translates both names, refuses an existing target without
     * MOVEFILE_REPLACE_EXISTING with ERROR_ALREADY_EXISTS, moves the file or directory, and sets
     * last error on failure.  Success leaves last error alone.
     *
     * The core's STOPS (a NULL lpNewFileName, every flag outside the three it builds, a move
     * between two host volumes with MOVEFILE_COPY_ALLOWED, a read-only target that is replaced)
     * are not guarded here: RT-TRAPS lists them.  The test proves each one but the move between
     * volumes, which it cannot arrange: all its files are on one. */
    return rt_file_move(RT_NAME_W(lpExistingFileName), RT_NAME_W(lpNewFileName), dwFlags);
}
