/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/previous-versions/windows/desktop/legacy/ee416893(v=vs.85)
 * RT-CASES:  writes NULL through ppDSC and returns DSERR_NODRIVER; logs once that there is no capture device (the host's microphone is never opened); NULL ppDSC returns E_INVALIDARG and writes nothing; leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/com.h"
#include "rt/win/mm.h"

SHIM(dsound, HRESULT, DirectSoundCaptureCreate, LPCGUID pcGuidDevice, void **ppDSC, void *pUnkOuter)
{
    /* This runtime has no capture device, and the host's microphone is never opened. The
     * capture object is the one thing that is never made: the out-pointer gets NULL, and the
     * answer is DSERR_NODRIVER ("No sound driver is available for use"). A NULL out-pointer is
     * a bad argument, so nothing is written. pcGuidDevice and pUnkOuter are not read. */
    (void)pcGuidDevice;
    (void)pUnkOuter;
    if (ppDSC == NULL)
        return E_INVALIDARG;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "dsound.dll!DirectSoundCaptureCreate",
                "DirectSoundCaptureCreate: there is no capture device (the host's microphone is never opened)");
    *ppDSC = NULL;
    return DSERR_NODRIVER;
}
