/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getservice
 * RT-CASES:  NULL ppv E_POINTER; not initialized: _NOT_INITIALIZED; IAudioRenderClient: a render client, S_OK;
 *            IAudioClock: a clock, S_OK; any other interface: NULL, E_NOINTERFACE, one log line naming it
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The GetService method accesses additional services from the audio client
 * object."  "E_POINTER: Parameter ppv is NULL."  "AUDCLNT_E_NOT_INITIALIZED: The audio stream has not been
 * successfully initialized."  "E_NOINTERFACE: The requested service interface is not supported."  The
 * services are IAudioRenderClient and IAudioClock, bound to this client (each holds a reference on it). */
COM_METHOD(IAudioClient, HRESULT, GetService, REFIID riid, void **ppv)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    pthread_mutex_lock(&s->mu);
    bool init = s->initialized;
    pthread_mutex_unlock(&s->mu);
    if (!init) return AUDCLNT_E_NOT_INITIALIZED;
    if (riid && rt_com_iface_by_iid(riid) == &rt_iface_IAudioRenderClient) {
        *ppv = rt_audio_render_new(This);
        return S_OK;
    }
    if (riid && rt_com_iface_by_iid(riid) == &rt_iface_IAudioClock) {
        *ppv = rt_audio_clock_new(This);
        return S_OK;
    }
    ac_log_no_interface("IAudioClient::GetService", riid);
    return E_NOINTERFACE;
}
