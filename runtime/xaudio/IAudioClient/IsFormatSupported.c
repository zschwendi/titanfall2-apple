/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-isformatsupported
 * RT-CASES:  NULL pFormat E_POINTER; exclusive: AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED; shared with NULL ppClosestMatch E_POINTER;
 *            shared, the mix format: NULL and S_OK; shared, anything else: a copy of the mix format and S_FALSE; other mode E_INVALIDARG
 * RT-TRAPS:  none
 */
#include "rt/heap.h"
#include "rt/mmdev.h"
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "For exclusive mode, set ppClosestMatch to NULL."  "For shared mode ...
 * set ppClosestMatch to point to a valid, non- NULL pointer variable."  "If the IsFormatSupported
 * call fails and ppClosestMatch is non- NULL, the method sets *ppClosestMatch to NULL."  The last
 * sentence is applied to the two failures the brief does not name (NULL pFormat and a mode out of
 * range): their *ppClosestMatch is set to NULL when the pointer is given.  The closest format of a
 * shared-mode mismatch is the mix format, the only one the endpoint has. */
COM_METHOD(IAudioClient, HRESULT, IsFormatSupported, AUDCLNT_SHAREMODE ShareMode, const WAVEFORMATEX *pFormat, WAVEFORMATEX **ppClosestMatch)
{
    (void)rt_com_state(ctx, This, &rt_audio_client_class);
    if (!pFormat) {
        if (ppClosestMatch) *ppClosestMatch = NULL;
        return E_POINTER;
    }
    if (ShareMode > AUDCLNT_SHAREMODE_EXCLUSIVE) {
        if (ppClosestMatch) *ppClosestMatch = NULL;
        return E_INVALIDARG;
    }
    if (ShareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
        if (ppClosestMatch) *ppClosestMatch = NULL;
        return AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED;
    }
    if (!ppClosestMatch) return E_POINTER;
    if (ac_format_is_mix(pFormat)) {
        *ppClosestMatch = NULL;
        return S_OK;
    }
    void *closest = rt_galloc(AC_MIX_FORMAT_BYTES);
    ac_fill_mix_format(closest);
    *ppClosestMatch = closest;
    return S_FALSE;
}
