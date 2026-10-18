/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-getcharacteristics
 * RT-CASES:  NULL E_POINTER; else 1 (AUDIOCLOCK_CHARACTERISTIC_FIXED_FREQ: the frequency does not change), S_OK
 * RT-TRAPS:  none
 */
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "AUDIOCLOCK_CHARACTERISTIC_FIXED_FREQ: The device frequency is fixed." (value 0x1) */
COM_METHOD(IAudioClock, HRESULT, GetCharacteristics, DWORD *pdwCharacteristics)
{
    (void)rt_com_state(ctx, This, &rt_audio_clock_class);
    if (!pdwCharacteristics) return E_POINTER;
    *pdwCharacteristics = 1;
    return S_OK;
}
