/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getbuffersize
 * RT-CASES:  not initialized: _NOT_INITIALIZED; NULL E_POINTER; else the buffer's size in frames, S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The GetBufferSize method retrieves the size (maximum capacity) of the endpoint
 * buffer."  The size is the frames Initialize gave (buffer_frames), fixed for the stream. */
COM_METHOD(IAudioClient, HRESULT, GetBufferSize, UINT32 *pNumBufferFrames)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    pthread_mutex_lock(&s->mu);
    bool init = s->initialized;
    uint32_t frames = s->buffer_frames;
    pthread_mutex_unlock(&s->mu);
    if (!init) return AUDCLNT_E_NOT_INITIALIZED;
    if (!pNumBufferFrames) return E_POINTER;
    *pNumBufferFrames = frames;
    return S_OK;
}
