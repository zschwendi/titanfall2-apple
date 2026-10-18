/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-getdevice
 * RT-CASES:  NULL id or out-pointer E_POINTER; the endpoint's id: a new device object, S_OK; any other id: E_NOTFOUND, *ppDevice NULL
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter pwstrId or ppDevice is NULL."; E_NOTFOUND "The
 * device ID does not identify an audio device that is in this system."  The id is compared
 * with the made-up endpoint's id (rt/mmdev.h), as UTF-16. */
COM_METHOD(IMMDeviceEnumerator, HRESULT, GetDevice, LPCWSTR pwstrId, IMMDevice **ppDevice)
{
    (void)rt_com_state(ctx, This, &rt_mm_enumerator_class);
    if (!pwstrId || !ppDevice) return E_POINTER;
    if (!rt_mm_id_is_endpoint(pwstrId)) {
        *ppDevice = NULL;
        return E_NOTFOUND;
    }
    *ppDevice = rt_mm_device_new();
    return S_OK;
}
