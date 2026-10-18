/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-start
 * RT-CASES:  not initialized: _NOT_INITIALIZED; started: _NOT_STOPPED; EVENTCALLBACK with no event: _EVENTHANDLE_NOT_SET;
 *            else the device thread starts, S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "AUDCLNT_E_NOT_STOPPED: The audio stream has not been stopped."  "AUDCLNT_E_EVENTHANDLE_NOT_SET:
 * The audio stream was initialized for event-driven buffering, but the client has not set the event handle."
 * The checks and the start are ac_device_start (runtime/xaudio/wasapi.c); a sink that cannot be opened is E_FAIL. */
COM_METHOD(IAudioClient, HRESULT, Start)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    return ac_device_start(s);
}
