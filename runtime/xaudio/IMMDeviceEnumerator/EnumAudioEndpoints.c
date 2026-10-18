/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-enumaudioendpoints
 * RT-CASES:  NULL out-pointer E_POINTER; flow above eAll or mask 0 or bits outside 0xF E_INVALIDARG; a collection of 1 for render or all with ACTIVE in the mask, else 0
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter ppDevices is NULL."; E_INVALIDARG "Parameter
 * dataFlow or dwStateMask is out of range."  The mask's valid bits are 0xF (DEVICE_STATEMASK_ALL);
 * 0 is out of range by the same rule as the other bits (a mask of no state).  The one endpoint
 * is an output that is active, so it counts when the flow is render or all and the mask has
 * DEVICE_STATE_ACTIVE. */
COM_METHOD(IMMDeviceEnumerator, HRESULT, EnumAudioEndpoints, EDataFlow dataFlow, DWORD dwStateMask, IMMDeviceCollection **ppDevices)
{
    (void)rt_com_state(ctx, This, &rt_mm_enumerator_class);
    if (!ppDevices) return E_POINTER;
    *ppDevices = NULL;
    if (dataFlow > MM_E_ALL) return E_INVALIDARG;
    if (dwStateMask == 0 || (dwStateMask & ~(DWORD)MM_DEVICE_STATEMASK_ALL)) return E_INVALIDARG;
    int render = dataFlow == MM_E_RENDER || dataFlow == MM_E_ALL;
    unsigned count = render && (dwStateMask & MM_DEVICE_STATE_ACTIVE) ? 1 : 0;
    *ppDevices = rt_mm_collection_new(count);
    return S_OK;
}
