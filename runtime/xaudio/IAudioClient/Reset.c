/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-reset
 * RT-CASES:  not initialized: _NOT_INITIALIZED; started: _NOT_STOPPED; a GetBuffer open: _BUFFER_OPERATION_PENDING;
 *            else the ring is emptied, padding and position are 0, S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "AUDCLNT_E_NOT_STOPPED: The audio stream has not been stopped." "AUDCLNT_E_BUFFER_OPERATION_PENDING:
 * The endpoint buffer is being accessed by the client." "The Reset method resets the audio stream."  The stream's
 * position is counted from the stream's start, so it starts again at 0. */
COM_METHOD(IAudioClient, HRESULT, Reset)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    pthread_mutex_lock(&s->mu);
    if (!s->initialized) { pthread_mutex_unlock(&s->mu); return AUDCLNT_E_NOT_INITIALIZED; }
    if (s->started) { pthread_mutex_unlock(&s->mu); return AUDCLNT_E_NOT_STOPPED; }
    if (s->open) { pthread_mutex_unlock(&s->mu); return AUDCLNT_E_BUFFER_OPERATION_PENDING; }
    s->padding = 0;
    s->head = 0;
    s->position = 0;
    pthread_mutex_unlock(&s->mu);
    return S_OK;
}
