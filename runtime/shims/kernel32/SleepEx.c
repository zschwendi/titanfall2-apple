/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-sleepex
 * RT-CASES:  returns 0 when the time has passed; sleeps the given number of milliseconds; zero gives up the processor and returns at once; alertable: queued APCs run in order and the call returns WAIT_IO_COMPLETION; a queued APC runs before sleeping, even with a time of 0; not alertable: a queued APC does not run; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/sync.h"

SHIM(kernel32, DWORD, SleepEx, DWORD dwMilliseconds, BOOL bAlertable)
{
    /* The core returns 0 when the time passed, or WAIT_IO_COMPLETION when an alertable wait
     * ran queued APCs instead; no last error is set on either result. */
    return rt_sleep_ex(ctx, dwMilliseconds, bAlertable);
}
