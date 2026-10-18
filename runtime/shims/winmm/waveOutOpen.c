/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutopen
 * RT-CASES:  WAVE_MAPPER returns MMSYSERR_NODRIVER; device 0 and device 7 return MMSYSERR_BADDEVICEID; a preset *phwo keeps its value; a NULL phwo answers the same; last error is left alone; logs that there is no audio output device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/log.h"
#include "rt/win/mm.h"              /* WAVE_MAPPER, HWAVEOUT, LPHWAVEOUT */

SHIM(winmm, MMRESULT, waveOutOpen, LPHWAVEOUT phwo, UINT uDeviceID, LPCVOID pwfx, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
    /* This runtime has no audio output device (docs/RUNTIME.md 11.1), so every identifier is out
     * of range and the mapper has nothing to choose from: WAVE_MAPPER gives MMSYSERR_NODRIVER and
     * any other identifier gives MMSYSERR_BADDEVICEID. Nothing is written through phwo and pwfx is
     * not read. An MMRESULT function does not touch last error, so none is set. */
    (void)phwo;
    (void)pwfx;
    (void)dwCallback;
    (void)dwInstance;
    (void)fdwOpen;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!waveOutOpen", "waveOutOpen: there is no audio output device");
    if (uDeviceID == WAVE_MAPPER) return MMSYSERR_NODRIVER;
    return MMSYSERR_BADDEVICEID;
}
