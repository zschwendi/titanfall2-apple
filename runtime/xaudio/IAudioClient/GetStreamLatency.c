/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getstreamlatency
 * RT-CASES:  not initialized: _NOT_INITIALIZED; NULL E_POINTER; else one period (100000, in 100 ns units), S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The GetStreamLatency method retrieves the maximum latency for the current stream
 * and can be called any time after the stream has been initialized."  The device takes one period at a time,
 * so the latency the stream reports is one period. */
COM_METHOD(IAudioClient, HRESULT, GetStreamLatency, REFERENCE_TIME *phnsLatency)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    pthread_mutex_lock(&s->mu);
    bool init = s->initialized;
    pthread_mutex_unlock(&s->mu);
    if (!init) return AUDCLNT_E_NOT_INITIALIZED;
    if (!phnsLatency) return E_POINTER;
    *phnsLatency = AC_DEVICE_PERIOD;
    return S_OK;
}
