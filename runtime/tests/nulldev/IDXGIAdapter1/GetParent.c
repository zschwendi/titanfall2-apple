/* Expected values: the object model of rt/nulldev.h (an adapter holds its factory) and COM's
 * rule that a returned interface carries a new reference.  The stops are this runtime's. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIAdapter1, GetParent)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    void *factory = NULL, *again = NULL;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;

    T_CASE("the adapter's factory as IDXGIFactory1, with one more reference");
    T_EQ(COM_CALL(IDXGIAdapter1, GetParent, adapter, &rt_iface_IDXGIFactory1.iid, &factory), S_OK);
    T_TRUE(rt_com_class_of(factory) == &rt_nulldev_factory_class);
    T_EQ(rt_com_refs(factory), 2);                      /* the adapter's and the caller's */
    T_EQ(COM_CALL(IDXGIAdapter1, GetParent, adapter, &rt_iface_IDXGIFactory1.iid, &again), S_OK);
    T_TRUE(again == factory);
    T_EQ(rt_com_refs(factory), 3);

    T_TRAPS(COM_CALL(IDXGIAdapter1, GetParent, adapter, &rt_iface_IDXGIAdapter1.iid, &again),
            "GetParent for {29038f61-3839-4626-91fd-...} (IDXGIAdapter1), which the parent, an object of class DXGIFactory, does not present");
    T_TRAPS(COM_CALL(IDXGIAdapter1, GetParent, adapter, NULL, &again), "GetParent with riid");
}
