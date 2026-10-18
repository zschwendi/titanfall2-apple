/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding
 * RT-CASES:  not initialized: _NOT_INITIALIZED; NULL E_POINTER; else the frames written by the game and not yet taken by the device, S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The GetCurrentPadding method retrieves the number of frames of padding in the
 * endpoint buffer."  The padding is the frames the game released into the ring and the device has not yet taken. */
COM_METHOD(IAudioClient, HRESULT, GetCurrentPadding, UINT32 *pNumPaddingFrames)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    pthread_mutex_lock(&s->mu);
    bool init = s->initialized;
    uint32_t pad = s->padding;
    pthread_mutex_unlock(&s->mu);
    if (!init) return AUDCLNT_E_NOT_INITIALIZED;
    if (!pNumPaddingFrames) return E_POINTER;
    *pNumPaddingFrames = pad;
    return S_OK;
}
