/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-stop
 * RT-CASES:  not initialized: _NOT_INITIALIZED; not started: S_FALSE; started: the device thread ends, S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The Stop method stops the audio stream."  "S_FALSE: The audio stream was not
 * running."  The frames in the ring stay (Reset empties it). */
COM_METHOD(IAudioClient, HRESULT, Stop)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    pthread_mutex_lock(&s->mu);
    bool init = s->initialized;
    bool started = s->started;
    pthread_mutex_unlock(&s->mu);
    if (!init) return AUDCLNT_E_NOT_INITIALIZED;
    if (!started) return S_FALSE;
    ac_device_stop_join(s);
    return S_OK;
}
