/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevicecollection-item
 * RT-CASES:  NULL E_POINTER; index at or past the count E_INVALIDARG, *ppdevice NULL; else a new device object, S_OK
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter ppDevice is NULL."; E_INVALIDARG "Parameter
 * nDevice is not a valid device number."  Each call makes a new device object (one reference). */
COM_METHOD(IMMDeviceCollection, HRESULT, Item, UINT nDevice, IMMDevice **ppdevice)
{
    MmCollection *c = rt_com_state(ctx, This, &rt_mm_collection_class);
    if (!ppdevice) return E_POINTER;
    if (nDevice >= c->count) {
        *ppdevice = NULL;
        return E_INVALIDARG;
    }
    *ppdevice = rt_mm_device_new();
    return S_OK;
}
