/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/en-us/search/?terms=DirectSoundCreate
 * RT-CASES:  NULL ppDS returns E_INVALIDARG and writes nothing; otherwise writes NULL through ppDS, logs once that there is no DirectSound output device (sound goes out through XAudio2), and returns DSERR_NODRIVER
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/com.h"
#include "rt/win/mm.h"

SHIM(dsound, HRESULT, DirectSoundCreate, LPCGUID pcGuidDevice, void **ppDS, void *pUnkOuter)
{
    /* The exact reference page could not be reached from the build host, so the sentence relied
     * on is written here. The reference says DirectSoundCreate returns DSERR_NODRIVER when no sound driver is
     * available for use. This runtime has no DirectSound output device: the game's sound goes
     * out through XAudio2, so the DirectSound object is the one thing that is never made. The
     * out-pointer gets NULL. A NULL out-pointer is a bad argument, so nothing is written.
     * pcGuidDevice and pUnkOuter are not read. */
    (void)pcGuidDevice;
    (void)pUnkOuter;
    if (ppDS == NULL)
        return E_INVALIDARG;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "dsound.dll!DirectSoundCreate",
                "DirectSoundCreate: there is no DirectSound output device (sound goes out through XAudio2)");
    *ppDS = NULL;
    return DSERR_NODRIVER;
}
