/* RT-STATUS: advisory
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
 * RT-CASES:  a period of 1 ms or more succeeds without effect; a period of 0 returns TIMERR_NOCANDO; the first accepted call is logged once at debug level; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(winmm, MMRESULT, timeBeginPeriod, UINT uPeriod)
{
    /* Windows documents the call as a request for a timer resolution.  The runtime's Sleep
     * is never rounded to the 15 ms tick, so there is nothing to do (docs/RUNTIME.md
     * sections 6.3 and 9.2).  A period of 0 is out of range. */
    if (uPeriod == 0) return TIMERR_NOCANDO;
    rt_log_once(RT_CH_THREAD, RT_LL_DEBUG, "timeBeginPeriod",
                "timeBeginPeriod(%u): accepted and ignored; Sleep is never rounded to the 15 ms tick", uPeriod);
    return TIMERR_NOERROR;
}
