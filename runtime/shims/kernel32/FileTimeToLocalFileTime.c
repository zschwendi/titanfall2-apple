/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-filetimetolocalfiletime
 * RT-CASES:  converts a UTC file time by the host's zone bias; stores both halves of the local time; the two pointers may be the same; a value of 0x8000000000000000 or more fails; a local time below zero or 0x8000000000000000 or more fails; does not change last error on success
 * RT-TRAPS:  NULL lpFileTime; NULL lpLocalFileTime
 */
#include "rt/shim.h"
#include "rt/time.h"

SHIM(kernel32, BOOL, FileTimeToLocalFileTime, const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime)
{
    if (lpFileTime == NULL) RT_UNIMPLEMENTED(ctx, "FileTimeToLocalFileTime with NULL lpFileTime");
    if (lpLocalFileTime == NULL) RT_UNIMPLEMENTED(ctx, "FileTimeToLocalFileTime with NULL lpLocalFileTime");
    /* The input is read before anything is stored, so the two pointers may be the same. */
    uint64_t value = ((uint64_t)lpFileTime->dwHighDateTime << 32) | lpFileTime->dwLowDateTime;
    if (value >= UINT64_C(0x8000000000000000)) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
    /* UTC = local + bias (minutes, positive west), so local = UTC - bias; one unit is 100 ns. */
    int64_t bias = (int64_t)rt_time_zone_bias_minutes() * 600000000;
    uint64_t local;
    if (bias > 0) {
        if (value < (uint64_t)bias) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
        local = value - (uint64_t)bias;
    } else {
        uint64_t add = (uint64_t)-bias;
        if (value > UINT64_C(0x7FFFFFFFFFFFFFFF) - add) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
        local = value + add;
    }
    lpLocalFileTime->dwLowDateTime = (DWORD)local;
    lpLocalFileTime->dwHighDateTime = (DWORD)(local >> 32);
    return TRUE;
}
