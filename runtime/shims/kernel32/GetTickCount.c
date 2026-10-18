/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount
 * RT-CASES:  returns the low 32 bits of the millisecond tick count; counts milliseconds; two calls in a row never go backwards; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/time.h"

SHIM(kernel32, DWORD, GetTickCount)
{
    /* The core's tick count, truncated to the DWORD the documentation specifies: the count
     * wraps to zero after 49.7 days. */
    return (DWORD)rt_time_tick_ms();
}
