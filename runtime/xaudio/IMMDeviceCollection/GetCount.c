/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevicecollection-getcount
 * RT-CASES:  NULL E_POINTER; else the count (0 or 1)
 * RT-TRAPS:  none
 */
#include "rt/mmdev.h"

/* RT-DOC, read 2026-10-12: E_POINTER "Parameter pcDevices is NULL." */
COM_METHOD(IMMDeviceCollection, HRESULT, GetCount, UINT *pcDevices)
{
    MmCollection *c = rt_com_state(ctx, This, &rt_mm_collection_class);
    if (!pcDevices) return E_POINTER;
    *pcDevices = c->count;
    return S_OK;
}
