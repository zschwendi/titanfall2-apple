/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/timezoneapi/nf-timezoneapi-filetimetosystemtime
 * RT-CASES:  converts a FILETIME to every member of SYSTEMTIME; milliseconds from the sub-second part; value 0 is 1601-01-01, a Monday; a value of 0x8000000000000000 or more fails and leaves the output alone; does not change last error on success
 * RT-TRAPS:  NULL lpFileTime; NULL lpSystemTime
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, FileTimeToSystemTime, const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime)
{
    if (lpFileTime == NULL) RT_UNIMPLEMENTED(ctx, "FileTimeToSystemTime with NULL lpFileTime");
    if (lpSystemTime == NULL) RT_UNIMPLEMENTED(ctx, "FileTimeToSystemTime with NULL lpSystemTime");
    uint64_t value = ((uint64_t)lpFileTime->dwHighDateTime << 32) | lpFileTime->dwLowDateTime;
    /* The page: "This value must be less than 0x8000000000000000. Otherwise, the function fails." */
    if (value >= UINT64_C(0x8000000000000000)) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
    rt_systemtime_from_filetime(value, lpSystemTime);
    return TRUE;
}
