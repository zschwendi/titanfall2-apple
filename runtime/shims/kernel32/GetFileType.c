/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfiletype
 * RT-CASES:  each standard handle is FILE_TYPE_CHAR and leaves last error alone; a file under the install root is FILE_TYPE_DISK and leaves last error alone; NULL, INVALID_HANDLE_VALUE and an event handle are FILE_TYPE_UNKNOWN with ERROR_INVALID_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, DWORD, GetFileType, HANDLE hFile)
{
    /* The core does all of it (rt_file_type, rt/file.h): FILE_TYPE_DISK for a file or a
     * directory, FILE_TYPE_CHAR for a character device (the standard handles, NUL), and for
     * anything that is not an open file handle FILE_TYPE_UNKNOWN with ERROR_INVALID_HANDLE
     * (set by the core).  Success leaves last error alone.
     *
     * Not modelled yet: the documentation says FILE_TYPE_PIPE for a pipe or a socket.  No unit
     * makes a pipe or socket handle yet, and the core answers any other kind with
     * FILE_TYPE_UNKNOWN, so this must be revisited when a pipe or socket unit lands. */
    return rt_file_type(hFile);
}
