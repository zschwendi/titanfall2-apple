/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlocaltime
 * RT-CASES:  writes the local wall clock as SYSTEMTIME, all eight members; does not change last error
 * RT-TRAPS:  NULL lpSystemTime
 */
#include "rt/shim.h"
#include "rt/locale.h"
#include "rt/time.h"

SHIM(kernel32, void, GetLocalTime, LPSYSTEMTIME lpSystemTime)
{
    if (lpSystemTime == NULL) RT_UNIMPLEMENTED(ctx, "GetLocalTime with NULL lpSystemTime");
    /* UTC = local time + bias, so local = UTC - bias; 1 minute is 600,000,000 units of 100 ns. */
    rt_systemtime_from_filetime(rt_time_utc_100ns() - (int64_t)rt_time_zone_bias_minutes() * 600000000, lpSystemTime);
}
