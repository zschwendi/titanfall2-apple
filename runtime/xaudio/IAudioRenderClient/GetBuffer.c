/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-getbuffer
 * RT-CASES:  NULL ppData E_POINTER; one open already: _OUT_OF_ORDER; more than the room (buffer_frames - padding): _BUFFER_TOO_LARGE
 *            and NULL; else the write buffer, the grant is n frames, S_OK (n 0 too)
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "AUDCLNT_E_BUFFER_TOO_LARGE: The NumFramesRequested value exceeds the available buffer
 * space (buffer size minus padding size)."  "AUDCLNT_E_OUT_OF_ORDER: A previous IAudioRenderClient::GetBuffer call
 * is still in effect."  "E_POINTER: Parameter ppData is NULL."  The write buffer is the same memory on every call,
 * and it holds the frames until ReleaseBuffer puts them in the ring. */
COM_METHOD(IAudioRenderClient, HRESULT, GetBuffer, UINT32 NumFramesRequested, BYTE **ppData)
{
    AcBound *b = rt_com_state(ctx, This, &rt_audio_render_class);
    AcClient *s = b->ac;
    if (!ppData) return E_POINTER;
    pthread_mutex_lock(&s->mu);
    if (s->open) {
        pthread_mutex_unlock(&s->mu);
        return AUDCLNT_E_OUT_OF_ORDER;
    }
    if (NumFramesRequested > s->buffer_frames - s->padding) {
        pthread_mutex_unlock(&s->mu);
        *ppData = NULL;
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    }
    s->open = true;
    s->granted = NumFramesRequested;
    *ppData = s->write_buf;
    pthread_mutex_unlock(&s->mu);
    return S_OK;
}
