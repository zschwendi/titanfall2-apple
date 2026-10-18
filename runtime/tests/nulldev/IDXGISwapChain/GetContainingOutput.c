/* Expected values: Microsoft's page of IDXGISwapChain::GetContainingOutput (read 2026-10-11:
 * a new reference on success, the output is owned by the device's adapter); one output is the
 * design's decision (design-null-device.md 3.4), so the result is the adapter's output;
 * the stop on a NULL out-pointer is the contract's choice (the page names no result). */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGISwapChain, GetContainingOutput)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGISwapChain *sc = rt_nulldev_new(device, &rt_nulldev_swapchain_class);
    IDXGIOutput *a = NULL, *b = NULL;

    T_CASE("a new output object with one reference, of the device's adapter");
    T_EQ(rt_com_refs(sc), 1);
    T_EQ(COM_CALL(IDXGISwapChain, GetContainingOutput, sc, &a), S_OK);
    T_TRUE(rt_com_class_of(a) == &rt_nulldev_output_class);
    T_EQ(rt_com_refs(a), 1);
    T_TRUE(((NdOutput *)rt_com_state_of(a, &rt_nulldev_output_class))->adapter == adapter);
    T_EQ(rt_com_refs(adapter), 2);                      /* the output holds its adapter */
    T_EQ(COM_CALL(IDXGISwapChain, GetContainingOutput, sc, &b), S_OK);
    T_CASE("two calls give two objects");
    T_TRUE(b != a);
    T_TRUE(rt_com_class_of(b) == &rt_nulldev_output_class);
    T_EQ(rt_com_refs(b), 1);
    T_TRUE(((NdOutput *)rt_com_state_of(b, &rt_nulldev_output_class))->adapter == adapter);
    T_EQ(rt_com_refs(adapter), 3);
    T_EQ(COM_CALL(IDXGIOutput, Release, b), 0);
    T_EQ(COM_CALL(IDXGIOutput, Release, a), 0);
    T_EQ(rt_com_refs(adapter), 1);
    T_EQ(rt_com_refs(sc), 1);

    T_TRAPS(COM_CALL(IDXGISwapChain, GetContainingOutput, sc, NULL), "GetContainingOutput with a NULL out-pointer");
}
