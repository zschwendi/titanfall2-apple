/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-releasebuffer
 * RT-CASES:  none open: _OUT_OF_ORDER unless n is 0 (then S_OK); n more than the grant: _INVALID_SIZE; a flag other than SILENT:
 *            E_INVALIDARG; else n frames go into the ring (silence with SILENT), the grant ends, S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "AUDCLNT_E_INVALID_SIZE: The NumFramesWritten value exceeds the NumFramesRequested value
 * specified in the previous IAudioRenderClient::GetBuffer call."  "AUDCLNT_E_OUT_OF_ORDER: The client called
 * ReleaseBuffer without a previous call to GetBuffer."  "AUDCLNT_BUFFERFLAGS_SILENT: Treat all of the data in the
 * packet as silence and ignore the data."  The ring has room: GetBuffer checked it against the padding, and only
 * the game's own calls add to the padding. */
COM_METHOD(IAudioRenderClient, HRESULT, ReleaseBuffer, UINT32 NumFramesWritten, DWORD dwFlags)
{
    AcBound *b = rt_com_state(ctx, This, &rt_audio_render_class);
    AcClient *s = b->ac;
    pthread_mutex_lock(&s->mu);
    if (!s->open) {
        pthread_mutex_unlock(&s->mu);
        return NumFramesWritten == 0 ? S_OK : AUDCLNT_E_OUT_OF_ORDER;
    }
    if (NumFramesWritten > s->granted) {
        pthread_mutex_unlock(&s->mu);
        return AUDCLNT_E_INVALID_SIZE;
    }
    if (dwFlags & ~AUDCLNT_BUFFERFLAGS_SILENT) {
        pthread_mutex_unlock(&s->mu);
        return E_INVALIDARG;
    }
    bool silent = (dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    const float *src = s->write_buf;
    for (uint32_t i = 0; i < NumFramesWritten; i++) {
        uint32_t idx = (s->head + s->padding + i) % s->buffer_frames;
        float *dst = s->ring + 2 * idx;
        if (silent) {
            dst[0] = 0.0f;
            dst[1] = 0.0f;
        } else {
            dst[0] = src[2 * i];
            dst[1] = src[2 * i + 1];
        }
    }
    s->padding += NumFramesWritten;
    if (NumFramesWritten) s->written = true;
    s->open = false;
    s->granted = 0;
    pthread_mutex_unlock(&s->mu);
    return S_OK;
}
