/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutsetvolume
 * RT-CASES:  returns MMSYSERR_INVALHANDLE for a NULL and a non-NULL device handle; returns MMSYSERR_INVALHANDLE whatever the volume; leaves last error alone; logs that there is no audio output device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/mm.h"              /* HWAVEOUT */

SHIM(winmm, MMRESULT, waveOutSetVolume, HWAVEOUT hwo, DWORD dwVolume)
{
    /* This runtime has no audio output device, so waveOutOpen never gives out a handle and
     * every handle is invalid, whatever the volume. Last error is not touched. */
    (void)hwo;
    (void)dwVolume;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!waveOutSetVolume", "waveOutSetVolume: there is no audio output device");
    return MMSYSERR_INVALHANDLE;
}
