/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-dosdatetimetofiletime
 * RT-CASES:  converts an MS-DOS date and time to a FILETIME, both halves written; refuses a date or time that does not exist with ERROR_INVALID_PARAMETER and writes nothing; does not change last error on success
 * RT-TRAPS:  NULL lpFileTime
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, DosDateTimeToFileTime, WORD wFatDate, WORD wFatTime, LPFILETIME lpFileTime)
{
    if (lpFileTime == NULL) RT_UNIMPLEMENTED(ctx, "DosDateTimeToFileTime with NULL lpFileTime");
    /* Unpack the packed MS-DOS date (day bits 0-4, month 5-8, year minus 1980 in 9-15) and time
     * (seconds divided by 2 in bits 0-4, minute 5-10, hour 11-15) into a SYSTEMTIME with no
     * milliseconds. No time zone is applied. */
    SYSTEMTIME st;
    st.wYear = (WORD)(1980 + (wFatDate >> 9));
    st.wMonth = (WORD)((wFatDate >> 5) & 0x0F);
    st.wDay = (WORD)(wFatDate & 0x1F);
    st.wDayOfWeek = 0;
    st.wHour = (WORD)(wFatTime >> 11);
    st.wMinute = (WORD)((wFatTime >> 5) & 0x3F);
    st.wSecond = (WORD)((wFatTime & 0x1F) * 2);
    st.wMilliseconds = 0;
    /* The core sets ERROR_INVALID_PARAMETER and writes nothing for a date or time that does not exist. */
    uint64_t ft;
    if (!rt_filetime_from_systemtime(&st, &ft)) return FALSE;
    lpFileTime->dwLowDateTime = (DWORD)ft;
    lpFileTime->dwHighDateTime = (DWORD)(ft >> 32);
    return TRUE;
}
