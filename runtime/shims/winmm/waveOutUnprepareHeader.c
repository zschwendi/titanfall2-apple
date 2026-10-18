/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutunprepareheader
 * RT-CASES:  returns MMSYSERR_INVALHANDLE for a NULL device handle; returns MMSYSERR_INVALHANDLE for a non-NULL device handle; leaves the header unchanged; leaves last error alone; logs that there is no audio output device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/log.h"
#include "rt/win/mm.h"              /* HWAVEOUT */

SHIM(winmm, MMRESULT, waveOutUnprepareHeader, HWAVEOUT hwo, LPVOID pwh, UINT cbwh)
{
    /* This runtime has no audio output device (docs/RUNTIME.md 11.1), so waveOutOpen never gives
     * out a handle and every device handle is invalid.  The header is neither read nor written. */
    (void)hwo;
    (void)pwh;
    (void)cbwh;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!waveOutUnprepareHeader",
                "waveOutUnprepareHeader: there is no audio output device");
    return MMSYSERR_INVALHANDLE;
}
