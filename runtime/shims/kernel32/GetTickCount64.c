/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64
 * RT-CASES:  returns the millisecond tick count as a 64-bit value; counts milliseconds; two calls in a row never go backwards; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/time.h"

SHIM(kernel32, ULONGLONG, GetTickCount64)
{
    /* The core's tick count, which is already in milliseconds and 64 bits. */
    return rt_time_tick_ms();
}
