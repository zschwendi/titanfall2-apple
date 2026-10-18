/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiadapter-getdesc
 * RT-CASES:  the adapter the native renderer reported on this Mac, every byte of the structure
 * RT-TRAPS:  GetDesc with a NULL description
 */
#include <string.h>

#include "rt/nulldev.h"

/* Fills all 304 bytes of DXGI_ADAPTER_DESC (layout: rt/win/dxgi.h) and returns S_OK.
 *
 * Every value is what the native renderer reported for this Mac's adapter (finding 35,
 * docs/findings/35-dxmt-build.md section 3; the ND_ADAPTER_* constants of rt/nulldev.h):
 * Description "Apple M3 Pro" as 16-bit units, zero-filled to 128; VendorId 0x106b; DeviceId,
 * SubSysId, Revision 0; DedicatedVideoMemory 7151124480; DedicatedSystemMemory and
 * SharedSystemMemory 0; AdapterLuid as printed there.  The game reads vendor and memory
 * (design-null-device.md 3.2); copying the native renderer's answers keeps its start-up
 * decisions the same when the backing changes at Gate 4.
 *
 * A NULL description: Microsoft's page gives E_INVALIDARG for it (from memory, not read
 * today), so it is a stop until someone reads the page. */
COM_METHOD(IDXGIAdapter1, HRESULT, GetDesc, DXGI_ADAPTER_DESC *desc)
{
    static const WCHAR name[] = ND_ADAPTER_DESCRIPTION;
    (void)rt_com_state(ctx, This, &rt_nulldev_adapter_class);
    if (!desc) RT_UNIMPLEMENTED(ctx, "GetDesc with a NULL description");
    memset(desc, 0, sizeof *desc);
    memcpy(desc->Description, name, sizeof name);
    desc->VendorId = ND_ADAPTER_VENDOR_ID;
    desc->DeviceId = ND_ADAPTER_DEVICE_ID;
    desc->SubSysId = ND_ADAPTER_SUBSYS_ID;
    desc->Revision = ND_ADAPTER_REVISION;
    desc->DedicatedVideoMemory = ND_ADAPTER_VIDEO_MEMORY;
    desc->DedicatedSystemMemory = 0;
    desc->SharedSystemMemory = 0;
    desc->AdapterLuid.LowPart = ND_ADAPTER_LUID_LOW;
    desc->AdapterLuid.HighPart = ND_ADAPTER_LUID_HIGH;
    return S_OK;
}
