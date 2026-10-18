/* Expected values: the object model of rt/nulldev.h (the device holds its adapter) and COM's
 * rule that a returned interface carries a new reference.  The stops are this runtime's:
 * Microsoft's page of IDXGIObject::GetParent (read 2026-10-10) names no result for them. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIDevice1, GetParent)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGIDevice1 *dxgi = NULL;
    void *adapter = NULL, *again = NULL;
    rt_nulldev_create(&device, &context);
    T_EQ(COM_CALL(ID3D11Device, QueryInterface, device, &rt_iface_IDXGIDevice1.iid, (void **)&dxgi), S_OK);

    T_CASE("the device's adapter as IDXGIAdapter1, with one more reference");
    T_EQ(COM_CALL(IDXGIDevice1, GetParent, dxgi, &rt_iface_IDXGIAdapter1.iid, &adapter), S_OK);
    T_TRUE(rt_com_class_of(adapter) == &rt_nulldev_adapter_class);
    T_TRUE(adapter == ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter);
    T_EQ(rt_com_refs(adapter), 2);                      /* the device's and the caller's */
    T_EQ(COM_CALL(IDXGIDevice1, GetParent, dxgi, &rt_iface_IDXGIAdapter1.iid, &again), S_OK);
    T_TRUE(again == adapter);
    T_EQ(rt_com_refs(adapter), 3);

    T_TRAPS(COM_CALL(IDXGIDevice1, GetParent, dxgi, &rt_iface_IDXGIFactory1.iid, &again),
            "GetParent for {770aae78-f26f-4dba-a829-...} (IDXGIFactory1), which the parent, an object of class DXGIAdapter, does not present");
    T_TRAPS(COM_CALL(IDXGIDevice1, GetParent, dxgi, &rt_iface_IDXGIAdapter1.iid, NULL), "GetParent with riid");
}
