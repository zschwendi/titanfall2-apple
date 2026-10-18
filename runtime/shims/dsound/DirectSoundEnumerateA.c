/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/en-us/search/?terms=DirectSoundEnumerateA
 * RT-CASES:  NULL callback returns E_INVALIDARG; otherwise the callback is never called, logs once that there is no DirectSound output device (sound goes out through XAudio2), and returns DS_OK (0)
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/win/com.h"
#include "rt/win/mm.h"

SHIM(dsound, HRESULT, DirectSoundEnumerateA, void *pDSEnumCallback, void *pContext)
{
    /* The exact reference page could not be reached from the build host, so the sentence relied
     * on is written here. The reference says DirectSoundEnumerate calls the callback once for each DirectSound
     * device it finds. This runtime has no device to report, not even a primary one, so the
     * callback is never called. The callback is a guest function pointer: it is not read or
     * called. A NULL callback is a bad argument. pContext is not read. The answer is DS_OK (0),
     * the success code the reference gives for an enumeration that found nothing. */
    (void)pContext;
    if (pDSEnumCallback == NULL)
        return E_INVALIDARG;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "dsound.dll!DirectSoundEnumerateA",
                "DirectSoundEnumerateA: there is no DirectSound output device to enumerate (sound goes out through XAudio2)");
    return S_OK;
}
