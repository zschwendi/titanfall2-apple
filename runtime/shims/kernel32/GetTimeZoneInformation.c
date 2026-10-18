/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/timezoneapi/nf-timezoneapi-gettimezoneinformation
 * RT-CASES:  fills Bias with the host's offset from UTC in minutes; reports TIME_ZONE_ID_UNKNOWN with zero biases, zero transition dates and empty names; does not change last error on success
 * RT-TRAPS:  NULL lpTimeZoneInformation
 */
#include <string.h>

#include "rt/shim.h"
#include "rt/time.h"
#include "rt/win/sysinfo.h"

SHIM(kernel32, DWORD, GetTimeZoneInformation, LPTIME_ZONE_INFORMATION lpTimeZoneInformation)
{
    /* The documentation does not say what a NULL pointer does: a stop, not a guess. */
    if (lpTimeZoneInformation == NULL)
        RT_UNIMPLEMENTED(ctx, "GetTimeZoneInformation with NULL lpTimeZoneInformation");

    /* The core gives only the current offset from UTC (rt/time.h).  The zone is reported as one
     * with no daylight-saving rule: TIME_ZONE_ID_UNKNOWN, zero biases, zero transition dates
     * (wMonth 0) and empty names, which the documentation allows.  Known approximation: on a host
     * whose zone uses daylight saving, Windows reports TIME_ZONE_ID_STANDARD or _DAYLIGHT with the
     * dates and biases; the core does not supply them, so this shim cannot. */
    memset(lpTimeZoneInformation, 0, sizeof *lpTimeZoneInformation);
    lpTimeZoneInformation->Bias = rt_time_zone_bias_minutes();
    return TIME_ZONE_ID_UNKNOWN;
}
