/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-seteventhandle
 * RT-CASES:  not initialized: _NOT_INITIALIZED; initialized without EVENTCALLBACK: _EVENTHANDLE_NOT_EXPECTED; NULL E_INVALIDARG;
 *            a handle that is not an event: E_INVALIDARG; an event: S_OK, the device signals it after each period
 * RT-TRAPS:  none
 */
#include "rt/handle.h"
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The SetEventHandle method sets the event handle that the system signals when an
 * audio buffer is ready to be processed by the client."  The event is a reference of the client's; a second
 * SetEventHandle replaces it and releases the first. */
COM_METHOD(IAudioClient, HRESULT, SetEventHandle, HANDLE eventHandle)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    pthread_mutex_lock(&s->mu);
    bool init = s->initialized;
    DWORD flags = s->flags;
    pthread_mutex_unlock(&s->mu);
    if (!init) return AUDCLNT_E_NOT_INITIALIZED;
    if (!(flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK)) return AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED;
    if (!eventHandle) return E_INVALIDARG;
    RtObject *ev = rt_handle_ref(eventHandle, RT_K_EVENT);
    if (!ev) return E_INVALIDARG;
    pthread_mutex_lock(&s->mu);
    RtObject *old = s->event;
    s->event = ev;
    pthread_mutex_unlock(&s->mu);
    if (old) rt_object_unref(old);
    return S_OK;
}
