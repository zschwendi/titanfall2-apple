/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-getdefaultaudioendpoint
 * RT-CASES:  NULL out-pointer E_POINTER; flow or role out of range E_INVALIDARG; eCapture: no device, E_NOTFOUND; eRender: a new device object, S_OK
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter ppDevice is NULL."; E_INVALIDARG "Parameter
 * dataFlow or role is out of range."; E_NOTFOUND "No device is available."  The one output
 * endpoint is made up (rt/mmdev.h); the microphone is never opened, so eCapture has none. */
COM_METHOD(IMMDeviceEnumerator, HRESULT, GetDefaultAudioEndpoint, EDataFlow dataFlow, ERole role, IMMDevice **ppEndpoint)
{
    (void)rt_com_state(ctx, This, &rt_mm_enumerator_class);
    if (!ppEndpoint) return E_POINTER;
    *ppEndpoint = NULL;
    if (dataFlow > MM_E_CAPTURE || role > MM_E_COMMUNICATIONS) return E_INVALIDARG;
    if (dataFlow == MM_E_CAPTURE) return E_NOTFOUND;
    *ppEndpoint = rt_mm_device_new();
    return S_OK;
}
