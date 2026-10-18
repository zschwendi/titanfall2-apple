/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutwrite
 * RT-CASES:  returns MMSYSERR_INVALHANDLE for a NULL and a non-NULL device handle; leaves the header unchanged; leaves last error alone; logs that there is no audio output device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/mm.h"              /* HWAVEOUT */

SHIM(winmm, MMRESULT, waveOutWrite, HWAVEOUT hwo, LPVOID pwh, UINT cbwh)
{
    /* This runtime has no audio output device, so waveOutOpen never gives out a handle and
     * every handle is invalid. The header is not read or written. Last error is not touched. */
    (void)hwo;
    (void)pwh;
    (void)cbwh;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!waveOutWrite", "waveOutWrite: there is no audio output device");
    return MMSYSERR_INVALHANDLE;
}
