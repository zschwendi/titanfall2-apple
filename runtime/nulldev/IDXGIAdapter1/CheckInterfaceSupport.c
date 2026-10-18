/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiadapter-checkinterfacesupport
 * RT-CASES:  the ID3D11Device IID returns DXGI_ERROR_UNSUPPORTED; umd_version is not written for the ID3D11Device IID, and the adapter's references are unchanged
 * RT-TRAPS:  CheckInterfaceSupport for the interface; CheckInterfaceSupport with a NULL interface
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "S_OK indicates that the interface is supported, otherwise
 * DXGI_ERROR_UNSUPPORTED is returned"; "If you try to use CheckInterfaceSupport to check
 * whether a Direct3D 11.x and later version interface is supported, CheckInterfaceSupport
 * returns DXGI_ERROR_UNSUPPORTED"; umd_version is "returned only if the interface is
 * supported".  So the ID3D11Device IID answers DXGI_ERROR_UNSUPPORTED and umd_version is not
 * written.  Every other GUID is a stop: a Direct3D 10 interface would answer S_OK with a
 * driver version, and this device has none to give.  A NULL guid is a stop. */
COM_METHOD(IDXGIAdapter1, HRESULT, CheckInterfaceSupport, REFGUID guid, LARGE_INTEGER *umd_version)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_adapter_class);
    (void)umd_version;
    if (!guid) RT_UNIMPLEMENTED(ctx, "CheckInterfaceSupport with a NULL interface");
    if (memcmp(guid, &rt_iface_ID3D11Device.iid, sizeof *guid) == 0) return DXGI_ERROR_UNSUPPORTED;
    const RtComIface *want = rt_com_iface_by_iid(guid);
    RT_UNIMPLEMENTED(ctx, "CheckInterfaceSupport for the interface {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x} (%s)",
                     (unsigned)guid->Data1, (unsigned)guid->Data2, (unsigned)guid->Data3,
                     guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
                     guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7],
                     want ? want->name : "an interface the runtime does not describe");
}
