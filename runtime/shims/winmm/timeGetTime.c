/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timegettime
 * RT-CASES:  two calls do not go backwards; the value lies within the (DWORD)rt_time_tick_ms() readings just before and after; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/time.h"

SHIM(winmm, DWORD, timeGetTime)
{
    /* Documented: the system time in milliseconds, the time since Windows started.  The
     * runtime's tick count is that clock.  The call cannot fail and sets no last error. */
    return (DWORD)rt_time_tick_ms();
}
