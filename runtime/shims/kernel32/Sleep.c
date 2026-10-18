/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-sleep
 * RT-CASES:  sleeps the given number of milliseconds; zero gives up the processor and returns at once; is not alertable, so a queued APC does not run; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, void, Sleep, DWORD dwMilliseconds)
{
    /* Not alertable: a queued APC is never run by Sleep, and the core's result is dropped
     * because Sleep returns nothing.  INFINITE is passed through: the core never times out. */
    (void)rt_sleep_ex(ctx, dwMilliseconds, FALSE);
}
