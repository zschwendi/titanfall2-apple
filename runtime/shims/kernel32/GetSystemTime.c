/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtime
 * RT-CASES:  writes the UTC wall clock as SYSTEMTIME, all eight members; does not change last error
 * RT-TRAPS:  NULL lpSystemTime
 */
#include "rt/shim.h"
#include "rt/locale.h"
#include "rt/time.h"

SHIM(kernel32, void, GetSystemTime, LPSYSTEMTIME lpSystemTime)
{
    if (lpSystemTime == NULL) RT_UNIMPLEMENTED(ctx, "GetSystemTime with NULL lpSystemTime (Windows raises an access violation; not modelled)");
    rt_systemtime_from_filetime(rt_time_utc_100ns(), lpSystemTime);
}
