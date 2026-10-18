/* Expected values: Microsoft's page of IDXGIAdapter::EnumOutputs (read 2026-10-10: a new
 * reference on success, DXGI_ERROR_NOT_FOUND past the last output; the code's number is
 * MinGW's, rt/win/dxgi.h); one output is the design's decision (design-null-device.md 3.4);
 * NULL in the out-pointer on "not found" is the contract's choice. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIAdapter1, EnumOutputs)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIOutput *a = NULL, *b = NULL;

    T_CASE("index 0 is the one output, a new object with one reference");
    T_EQ(rt_com_refs(adapter), 1);
    T_EQ(COM_CALL(IDXGIAdapter1, EnumOutputs, adapter, 0, &a), S_OK);
    T_TRUE(rt_com_class_of(a) == &rt_nulldev_output_class);
    T_EQ(rt_com_refs(a), 1);
    T_TRUE(((NdOutput *)rt_com_state_of(a, &rt_nulldev_output_class))->adapter == adapter);
    T_EQ(rt_com_refs(adapter), 2);                      /* the output holds its adapter */
    T_EQ(COM_CALL(IDXGIAdapter1, EnumOutputs, adapter, 0, &b), S_OK);
    T_TRUE(b != a);
    T_EQ(COM_CALL(IDXGIOutput, Release, b), 0);
    T_EQ(rt_com_refs(adapter), 2);

    T_CASE("index 1 and above is DXGI_ERROR_NOT_FOUND");
    b = a;
    T_EQ((uint32_t)COM_CALL(IDXGIAdapter1, EnumOutputs, adapter, 1, &b), 0x887a0002u);
    T_TRUE(b == NULL);
    b = a;
    T_EQ((uint32_t)COM_CALL(IDXGIAdapter1, EnumOutputs, adapter, 0xffffffffu, &b), 0x887a0002u);
    T_TRUE(b == NULL);
    T_EQ(rt_com_refs(adapter), 2);

    T_TRAPS(COM_CALL(IDXGIAdapter1, EnumOutputs, adapter, 0, NULL), "EnumOutputs with a NULL out-pointer (index 0)");
}
