/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-getposition
 * RT-CASES:  NULL pu64Position E_POINTER; else the frames the device has taken, and the QPC time now (100 ns units) when pu64QPCPosition is given, S_OK
 * RT-TRAPS:  none
 */
#include "rt/time.h"
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "The GetPosition method retrieves the current device position."  "pu64QPCPosition: the
 * value of the performance counter at the time the device position was read, in 100-nanosecond units" (the
 * QueryPerformanceCounter of this runtime is rt_time_mono_100ns).  The position is the device's count, so it grows
 * by a period at each signal even when the game writes nothing. */
COM_METHOD(IAudioClock, HRESULT, GetPosition, UINT64 *pu64Position, UINT64 *pu64QPCPosition)
{
    AcBound *b = rt_com_state(ctx, This, &rt_audio_clock_class);
    if (!pu64Position) return E_POINTER;
    pthread_mutex_lock(&b->ac->mu);
    *pu64Position = b->ac->position;
    pthread_mutex_unlock(&b->ac->mu);
    if (pu64QPCPosition) *pu64QPCPosition = rt_time_mono_100ns();
    return S_OK;
}
