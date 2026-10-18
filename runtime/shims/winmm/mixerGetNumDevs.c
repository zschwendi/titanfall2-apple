/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-mixergetnumdevs
 * RT-CASES:  returns 0 when there is no mixer device; returns 0 on the second of two calls in a row; leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(winmm, UINT, mixerGetNumDevs)
{
    /* This runtime has no audio device (docs/RUNTIME.md 11.1), so the count is zero: the true
     * number of mixer devices, which Windows documents as zero when none is available. */
    return 0;
}
