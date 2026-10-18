/* RT-STATUS: advisory
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timeendperiod
 * RT-CASES:  a period of 1 ms or more succeeds without effect; a period of 0 returns TIMERR_NOCANDO; the first accepted call is logged once at debug level; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(winmm, MMRESULT, timeEndPeriod, UINT uPeriod)
{
    /* Windows documents the call as the end of a timer resolution set by timeBeginPeriod.
     * timeBeginPeriod changes nothing here (the runtime's Sleep is never rounded to the 15 ms
     * tick), so there is nothing to undo.  Whether the period matches an earlier
     * timeBeginPeriod is not checked.  A period of 0 is out of range. */
    if (uPeriod == 0) return TIMERR_NOCANDO;
    rt_log_once(RT_CH_THREAD, RT_LL_DEBUG, "timeEndPeriod",
                "timeEndPeriod(%u): accepted and ignored", uPeriod);
    return TIMERR_NOERROR;
}
