/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter
 * RT-CASES:  writes the count and returns TRUE; the count is in 100 ns units; two calls in a row never go backwards; does not change last error
 * RT-TRAPS:  NULL lpPerformanceCount
 */
#include "rt/shim.h"
#include "rt/time.h"

SHIM(kernel32, BOOL, QueryPerformanceCounter, LARGE_INTEGER *lpPerformanceCount)
{
    if (lpPerformanceCount == NULL) RT_UNIMPLEMENTED(ctx, "QueryPerformanceCounter with NULL lpPerformanceCount");
    /* The frequency is 10,000,000 counts a second (QueryPerformanceFrequency), so one count is
     * 100 ns, the unit of the core's monotonic clock: the count is that clock, unchanged. */
    lpPerformanceCount->QuadPart = (LONGLONG)rt_time_mono_100ns();
    return TRUE;
}
