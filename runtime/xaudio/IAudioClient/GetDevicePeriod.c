/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getdeviceperiod
 * RT-CASES:  both NULL E_POINTER; else each non-NULL pointer gets its period, S_OK
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "At least one of the two parameters, phnsDefaultDevicePeriod and
 * phnsMinimumDevicePeriod, must be non- NULL or the method returns immediately with error code
 * E_POINTER."  Each pointer that is not NULL gets its value, in units of 100 ns. */
COM_METHOD(IAudioClient, HRESULT, GetDevicePeriod, REFERENCE_TIME *phnsDefaultDevicePeriod, REFERENCE_TIME *phnsMinimumDevicePeriod)
{
    (void)rt_com_state(ctx, This, &rt_audio_client_class);
    if (!phnsDefaultDevicePeriod && !phnsMinimumDevicePeriod) return E_POINTER;
    if (phnsDefaultDevicePeriod) *phnsDefaultDevicePeriod = AC_DEVICE_PERIOD;
    if (phnsMinimumDevicePeriod) *phnsMinimumDevicePeriod = AC_MIN_PERIOD;
    return S_OK;
}
