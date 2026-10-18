/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevice-getstate
 * RT-CASES:  NULL E_POINTER; else DEVICE_STATE_ACTIVE
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter pdwState is NULL."  The made-up endpoint is
 * always active (nothing ever changes about it). */
COM_METHOD(IMMDevice, HRESULT, GetState, DWORD *pdwState)
{
    (void)rt_com_state(ctx, This, &rt_mm_device_class);
    if (!pdwState) return E_POINTER;
    *pdwState = MM_DEVICE_STATE_ACTIVE;
    return S_OK;
}
