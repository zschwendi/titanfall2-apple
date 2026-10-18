/* RT-STATUS: advisory
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/avrt/nf-avrt-avrevertmmthreadcharacteristics
 * RT-CASES:  returns TRUE for the task handle; returns FALSE with ERROR_INVALID_HANDLE for NULL and for any other handle; leaves last error alone on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/mm.h"              /* RT_AVRT_TASK */

SHIM(avrt, BOOL, AvRevertMmThreadCharacteristics, HANDLE AvrtHandle)
{
    /* The only handle this runtime gives out is RT_AVRT_TASK; any other value, NULL included,
     * is not a task. The call logs nothing of its own. */
    if (AvrtHandle != RT_AVRT_TASK)
        return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
    return TRUE;
}
