/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/timezoneapi/nf-timezoneapi-systemtimetofiletime
 * RT-CASES:  converts a SYSTEMTIME to a FILETIME, both halves written; refuses an invalid time with ERROR_INVALID_PARAMETER and writes nothing; ignores wDayOfWeek; does not change last error on success
 * RT-TRAPS:  NULL lpSystemTime; NULL lpFileTime
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, SystemTimeToFileTime, const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime)
{
    if (lpSystemTime == NULL) RT_UNIMPLEMENTED(ctx, "SystemTimeToFileTime with NULL lpSystemTime");
    if (lpFileTime == NULL) RT_UNIMPLEMENTED(ctx, "SystemTimeToFileTime with NULL lpFileTime");
    /* The core sets ERROR_INVALID_PARAMETER and writes nothing for an invalid time. */
    uint64_t ft;
    if (!rt_filetime_from_systemtime(lpSystemTime, &ft)) return FALSE;
    lpFileTime->dwLowDateTime = (DWORD)ft;
    lpFileTime->dwHighDateTime = (DWORD)(ft >> 32);
    return TRUE;
}
