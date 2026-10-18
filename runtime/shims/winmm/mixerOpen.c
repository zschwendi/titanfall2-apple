/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-mixeropen
 * RT-CASES:  returns MMSYSERR_BADDEVICEID for identifier 0 with flags 0; returns MMSYSERR_NODRIVER for identifier 0 with flags 0x10000000; leaves *phmx unchanged; leaves last error alone; logs that there is no mixer device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/mm.h"              /* MIXER_OBJECTF_TYPEMASK, MIXER_OBJECTF_MIXER */

SHIM(winmm, MMRESULT, mixerOpen, LPHMIXER phmx, UINT uMxId, DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
    /* This runtime has no audio device (docs/RUNTIME.md 11.1). A mixer identifier is valid
     * only if a device exists, so none is valid: the object-type bits of fdwOpen say whether
     * uMxId names a mixer device (BADDEVICEID) or some other object (NODRIVER). *phmx is not
     * written, and the last error is not set (the result is returned). */
    (void)phmx;
    (void)uMxId;
    (void)dwCallback;
    (void)dwInstance;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!mixerOpen", "mixerOpen: there is no mixer device");
    if ((fdwOpen & MIXER_OBJECTF_TYPEMASK) == MIXER_OBJECTF_MIXER)
        return MMSYSERR_BADDEVICEID;
    return MMSYSERR_NODRIVER;
}
