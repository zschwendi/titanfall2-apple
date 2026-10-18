/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutclose
 * RT-CASES:  returns MMSYSERR_INVALHANDLE for NULL; returns MMSYSERR_INVALHANDLE for handle 0x44; leaves last error alone; leaves a 48-byte header preset to 0xA5 unchanged; logs that there is no audio output device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/mm.h"              /* HWAVEOUT */

SHIM(winmm, MMRESULT, waveOutClose, HWAVEOUT hwo)
{
    /* waveOutOpen never gives out a handle, so every handle is invalid, NULL included. This
     * machine has no audio output device. The call returns its result and never touches last
     * error, and it writes nothing through any pointer. */
    (void)hwo;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!waveOutClose", "waveOutClose: there is no audio output device");
    return MMSYSERR_INVALHANDLE;
}
