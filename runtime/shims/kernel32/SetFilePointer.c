/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-setfilepointer
 * RT-CASES:  a NULL lpDistanceToMoveHigh moves the position from the beginning, the current position or the end and returns the new position; with lpDistanceToMoveHigh the distance is 64 bits and the high part of the new position is stored there; a position before the start fails with ERROR_NEGATIVE_SEEK, returns INVALID_SET_FILE_POINTER and leaves the position and the high value unchanged; a position of 0xFFFFFFFF is returned as it is and last error is then ERROR_SUCCESS; a move method other than the three fails with ERROR_INVALID_PARAMETER; a handle that is not open fails with ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  SetFilePointer with NULL lpDistanceToMoveHigh and a position above 0xFFFFFFFF; core: character device handle; core: directory handle
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, DWORD, SetFilePointer, HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
    /* The distance, as the 32-bit protocol gives it.  With lpDistanceToMoveHigh the high part is
     * the signed upper 32 bits and the low part is unsigned; the sum is written as arithmetic
     * (a left shift of a negative value is undefined in C11), which is the same number.  With
     * NULL the distance is the signed 32-bit value. */
    LONGLONG dist = lpDistanceToMoveHigh
        ? (LONGLONG)*lpDistanceToMoveHigh * ((LONGLONG)1 << 32) + (LONGLONG)(DWORD)lDistanceToMove
        : (LONGLONG)lDistanceToMove;
    LONGLONG pos;

    /* One core call.  On failure the core has set last error and moved nothing; the high pointer
     * is not written. */
    if (!rt_file_seek(hFile, dist, &pos, dwMoveMethod))
        return INVALID_SET_FILE_POINTER;

    /* With NULL the position cannot be returned beyond 32 bits.  The page says the call fails
     * then, but names no code, and the core has already moved the position.  Not guessed. */
    if (!lpDistanceToMoveHigh && pos > (LONGLONG)0xFFFFFFFFu)
        RT_UNIMPLEMENTED(ctx, "SetFilePointer with NULL lpDistanceToMoveHigh and a position above 0xFFFFFFFF");

    if (lpDistanceToMoveHigh)
        *lpDistanceToMoveHigh = (LONG)(pos >> 32);

    /* A new position of 0xFFFFFFFF is also the failure value: the caller tells success from
     * failure by last error, so success sets ERROR_SUCCESS here.  Other successes leave it alone. */
    if ((DWORD)pos == INVALID_SET_FILE_POINTER)
        rt_set_last_error(ctx, ERROR_SUCCESS);

    return (DWORD)pos;
}
