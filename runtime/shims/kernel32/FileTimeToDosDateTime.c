/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-filetimetodosdatetime
 * RT-CASES:  converts a file time to the MS-DOS date and time; an odd second rounds down; the first and last DOS dates convert; a time before 1980 or after 2107 fails and writes neither WORD; success does not change last error
 * RT-TRAPS:  NULL lpFileTime; NULL lpFatDate; NULL lpFatTime
 */
#include "rt/shim.h"
#include "rt/locale.h"

/* The MS-DOS date range, 1/1/1980 to 12/31/2107, as FILETIMEs: 1980-01-01 00:00:00 UTC and
 * 2108-01-01 00:00:00 UTC.  The range is the half-open [FT_DOS_FIRST, FT_DOS_END).  The
 * test checks both values against rt_time_filetime_from_unix. */
#define FT_DOS_FIRST  UINT64_C(119600064000000000)
#define FT_DOS_END    UINT64_C(159992928000000000)

SHIM(kernel32, BOOL, FileTimeToDosDateTime, const FILETIME *lpFileTime, LPWORD lpFatDate, LPWORD lpFatTime)
{
    RtSystemTime st;
    uint64_t t;

    if (lpFileTime == NULL) RT_UNIMPLEMENTED(ctx, "FileTimeToDosDateTime with NULL lpFileTime");
    if (lpFatDate == NULL) RT_UNIMPLEMENTED(ctx, "FileTimeToDosDateTime with NULL lpFatDate");
    if (lpFatTime == NULL) RT_UNIMPLEMENTED(ctx, "FileTimeToDosDateTime with NULL lpFatTime");

    t = ((uint64_t)lpFileTime->dwHighDateTime << 32) | lpFileTime->dwLowDateTime;
    /* Outside the range is a failure with nothing written.  That includes every value of
     * 0x8000000000000000 or more, and the core is called only for years 1980 to 2107, which
     * are inside the years it is valid for.  No time zone is applied. */
    if (t < FT_DOS_FIRST || t >= FT_DOS_END) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);

    rt_systemtime_from_filetime(t, &st);
    /* Date: day bits 0-4, month bits 5-8, year minus 1980 bits 9-15.
     * Time: seconds divided by 2 bits 0-4, minutes bits 5-10, hours bits 11-15. */
    *lpFatDate = (WORD)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
    *lpFatTime = (WORD)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    return TRUE;
}
