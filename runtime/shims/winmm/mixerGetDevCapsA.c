/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-mixergetdevcapsa
 * RT-CASES:  returns MMSYSERR_BADDEVICEID for identifier 0; returns MMSYSERR_BADDEVICEID for identifier 5; writes nothing through pmxcaps; leaves last error alone; logs that there is no mixer device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(winmm, MMRESULT, mixerGetDevCapsA, UINT_PTR uMxId, LPVOID pmxcaps, UINT cbmxcaps)
{
    /* This runtime has no audio device (RUNTIME.md 11.1): there are zero mixer devices and no
     * open mixer handle, so every identifier is out of range. pmxcaps is not written. */
    (void)uMxId;
    (void)pmxcaps;
    (void)cbmxcaps;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!mixerGetDevCapsA", "mixerGetDevCapsA: there is no mixer device");
    return MMSYSERR_BADDEVICEID;
}
