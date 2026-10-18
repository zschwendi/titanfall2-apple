/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/timezoneapi/nf-timezoneapi-systemtimetotzspecificlocaltime
 * RT-CASES:  converts a universal time with a zone given by the caller; uses the active zone when the zone is NULL; refuses an invalid universal time and writes nothing; success leaves last error alone
 * RT-TRAPS:  NULL lpUniversalTime; NULL lpLocalTime; zone with a daylight rule; local time before 1601-01-01 or after 30827-12-31
 */
#include "rt/shim.h"
#include "rt/win/sysinfo.h"
#include "rt/locale.h"
#include "rt/time.h"

#define UNITS_PER_MINUTE  UINT64_C(600000000)           /* 100 ns units in one minute */
#define LOCAL_LIMIT       UINT64_C(0x7FFF35F4F06C58F0)  /* local times from here on are past the year 30827 */

SHIM(kernel32, BOOL, SystemTimeToTzSpecificLocalTime, const TIME_ZONE_INFORMATION *lpTimeZoneInformation, const SYSTEMTIME *lpUniversalTime, LPSYSTEMTIME lpLocalTime)
{
    uint64_t utc, local, d;
    LONG bias;

    if (lpUniversalTime == NULL) RT_UNIMPLEMENTED(ctx, "SystemTimeToTzSpecificLocalTime with NULL lpUniversalTime");
    if (lpLocalTime == NULL) RT_UNIMPLEMENTED(ctx, "SystemTimeToTzSpecificLocalTime with NULL lpLocalTime");

    /* An invalid universal time is refused whatever the zone; the core sets ERROR_INVALID_PARAMETER. */
    if (!rt_filetime_from_systemtime(lpUniversalTime, &utc)) return FALSE;

    if (lpTimeZoneInformation == NULL) {
        bias = rt_time_zone_bias_minutes();
    } else {
        if (lpTimeZoneInformation->StandardDate.wMonth != 0 || lpTimeZoneInformation->DaylightDate.wMonth != 0)
            RT_UNIMPLEMENTED(ctx, "SystemTimeToTzSpecificLocalTime: zone with a daylight rule (a transition date with wMonth not 0)");
        bias = lpTimeZoneInformation->Bias;
    }

    /* local = UTC - bias minutes, in unsigned arithmetic so that no value overflows. */
    if (bias >= 0) {
        d = (uint64_t)bias * UNITS_PER_MINUTE;
        if (utc < d) RT_UNIMPLEMENTED(ctx, "SystemTimeToTzSpecificLocalTime: local time before 1601-01-01 or after 30827-12-31");
        local = utc - d;
    } else {
        d = (uint64_t)(-(int64_t)bias) * UNITS_PER_MINUTE;
        local = utc + d;
    }
    if (local >= LOCAL_LIMIT) RT_UNIMPLEMENTED(ctx, "SystemTimeToTzSpecificLocalTime: local time before 1601-01-01 or after 30827-12-31");

    rt_systemtime_from_filetime(local, lpLocalTime);
    return TRUE;
}
