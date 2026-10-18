/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-getfrequency
 * RT-CASES:  NULL E_POINTER; else 48000 (the stream's rate: a position divided by it is seconds), S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12 (IAudioClock::GetFrequency): "The frequency of the audio stream's device position, in
 * units of frames per second."  The position counts frames at the stream's rate. */
COM_METHOD(IAudioClock, HRESULT, GetFrequency, UINT64 *pu64Frequency)
{
    (void)rt_com_state(ctx, This, &rt_audio_clock_class);
    if (!pu64Frequency) return E_POINTER;
    *pu64Frequency = AC_MIX_RATE;
    return S_OK;
}
