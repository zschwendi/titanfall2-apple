/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutrestart
 * RT-CASES:  returns MMSYSERR_INVALHANDLE for a NULL handle; returns MMSYSERR_INVALHANDLE for handle 0x44; leaves a 48-byte header buffer alone; leaves last error alone; logs that there is no audio output device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/mm.h"

SHIM(winmm, MMRESULT, waveOutRestart, HWAVEOUT hwo)
{
    /* This runtime has no audio device (RUNTIME.md 11.1), and waveOutOpen never gives out a
     * handle, so every handle is invalid: the call fails as Windows documents for an invalid
     * device handle. */
    (void)hwo;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!waveOutRestart", "waveOutRestart: there is no audio output device");
    return MMSYSERR_INVALHANDLE;
}
