/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency
 * RT-CASES:  writes 10000000 counts a second and returns TRUE; the same value on every call; does not change last error
 * RT-TRAPS:  NULL lpFrequency
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, QueryPerformanceFrequency, LARGE_INTEGER *lpFrequency)
{
    if (lpFrequency == NULL) RT_UNIMPLEMENTED(ctx, "QueryPerformanceFrequency with NULL lpFrequency");
    /* Fixed at boot and the same on every processor (the documentation).  The value is the
     * design's 10 MHz (docs/RUNTIME.md section 8.1), the rate of QueryPerformanceCounter. */
    lpFrequency->QuadPart = 10000000;
    return TRUE;
}
