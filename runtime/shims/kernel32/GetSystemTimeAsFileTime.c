/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimeasfiletime
 * RT-CASES:  writes the UTC wall clock as a FILETIME; both halves of the FILETIME are written; does not change last error
 * RT-TRAPS:  NULL lpSystemTimeAsFileTime
 */
#include "rt/shim.h"
#include "rt/time.h"

SHIM(kernel32, void, GetSystemTimeAsFileTime, LPFILETIME lpSystemTimeAsFileTime)
{
    if (lpSystemTimeAsFileTime == NULL) RT_UNIMPLEMENTED(ctx, "GetSystemTimeAsFileTime with NULL lpSystemTimeAsFileTime");
    /* 100 ns intervals since 1601-01-01 UTC: the core's wall clock is already in this unit. */
    uint64_t t = rt_time_utc_100ns();
    lpSystemTimeAsFileTime->dwLowDateTime = (DWORD)t;
    lpSystemTimeAsFileTime->dwHighDateTime = (DWORD)(t >> 32);
}
