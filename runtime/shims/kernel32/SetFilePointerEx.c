/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-setfilepointerex
 * RT-CASES:  moves the position from the beginning, the current position or the end, and stores the new position; a NULL lpNewFilePointer moves the position and stores nothing; a position before the start fails with ERROR_NEGATIVE_SEEK and leaves the position and the stored value unchanged; a position beyond the end is allowed and does not change the file size; a move method other than the three fails with ERROR_INVALID_PARAMETER; a handle that is not open fails with ERROR_INVALID_HANDLE; success does not change last error
 * RT-TRAPS:  FILE_BEGIN with a negative distance; core: character device handle; core: directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, SetFilePointerEx, HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
{
    /* The documentation reads the distance as unsigned for FILE_BEGIN, so a negative one is a
     * position at the top of the 64-bit range.  What Windows does with that is not in the
     * documentation, and the core reads the distance as signed.  Not guessed: refused. */
    if (dwMoveMethod == FILE_BEGIN && liDistanceToMove.QuadPart < 0)
        RT_UNIMPLEMENTED(ctx, "FILE_BEGIN with a negative distance (%p): the documentation reads the distance as unsigned, and does not say what follows",
                         (void *)liDistanceToMove.QuadPart);

    /* The core does all of it (rt_file_seek, rt/file.h): one host seek, the new position stored
     * only on success, last error set on failure and left alone on success.  A character device
     * or a directory handle STOPS in the core: no guard here. */
    return rt_file_seek(hFile, liDistanceToMove.QuadPart,
                        lpNewFilePointer ? &lpNewFilePointer->QuadPart : NULL, dwMoveMethod);
}
