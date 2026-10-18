/* Expected values: Microsoft's page of IDXGIAdapter::CheckInterfaceSupport (read 2026-10-10:
 * S_OK when the interface is supported, otherwise DXGI_ERROR_UNSUPPORTED, whose code is
 * 0x887A0004; umd_version is "returned only if the interface is supported", so it is left
 * alone).  That the ID3D11Device IID is the one interface answered, and that every other
 * GUID and a NULL guid are stops, is the design note in the body.  umd_version starts as
 * 0x1122334455667788: any write by the body changes it.  The adapter's count of 1 before any
 * call is the one the EnumOutputs test asserts for a fresh device (rt_nulldev_create). */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIAdapter1, CheckInterfaceSupport)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    LARGE_INTEGER umd;
    umd.QuadPart = 0x1122334455667788LL;

    T_CASE("the ID3D11Device IID returns DXGI_ERROR_UNSUPPORTED");
    T_EQ((uint32_t)COM_CALL(IDXGIAdapter1, CheckInterfaceSupport, adapter, &rt_iface_ID3D11Device.iid, &umd), 0x887a0004u);

    T_CASE("umd_version is not written for the ID3D11Device IID, and the adapter's references are unchanged");
    T_EQ(umd.QuadPart, 0x1122334455667788LL);
    T_EQ(rt_com_refs(adapter), 1);

    T_TRAPS(COM_CALL(IDXGIAdapter1, CheckInterfaceSupport, adapter, &rt_iface_IDXGIDevice1.iid, &umd),
            "CheckInterfaceSupport for the interface {77db970f-6276-48ba-ba28-070143b4392c} (IDXGIDevice1)");
    T_TRAPS(COM_CALL(IDXGIAdapter1, CheckInterfaceSupport, adapter, NULL, &umd), "CheckInterfaceSupport with a NULL interface");
}
