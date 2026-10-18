/* Expected values: Microsoft's page of IDXGISwapChain::GetFullscreenState (read 2026-10-10:
 * pFullscreen and ppTarget are both optional out-pointers; ppTarget is NULL when the mode is
 * not full screen; the reference count rises only in full-screen mode).  This swap chain is
 * always windowed (finding 31, section 2), so the values are FALSE and NULL.  The page lists
 * no return code; S_OK is from the notes.  The out-pointers start as sentinels (-1 and a real
 * output of the device's adapter, from EnumOutputs), so a write that is missing or partial
 * shows. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGISwapChain, GetFullscreenState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGISwapChain *sc = rt_nulldev_new(device, &rt_nulldev_swapchain_class);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIOutput *out = NULL;
    T_EQ(COM_CALL(IDXGIAdapter1, EnumOutputs, adapter, 0, &out), S_OK);
    BOOL fs;
    IDXGIOutput *target;

    T_CASE("GetFullscreenState writes FALSE and NULL over other values and moves no reference");
    fs = -1;
    target = out;
    T_EQ(COM_CALL(IDXGISwapChain, GetFullscreenState, sc, &fs, &target), S_OK);
    T_EQ(fs, FALSE);
    T_TRUE(target == NULL);
    T_EQ(rt_com_refs(out), 1);
    T_EQ(rt_com_refs(sc), 1);

    T_CASE("each out-pointer may be NULL, and the other is still written");
    fs = -1;
    T_EQ(COM_CALL(IDXGISwapChain, GetFullscreenState, sc, NULL, NULL), S_OK);
    T_EQ(COM_CALL(IDXGISwapChain, GetFullscreenState, sc, &fs, NULL), S_OK);
    T_EQ(fs, FALSE);
    target = out;
    T_EQ(COM_CALL(IDXGISwapChain, GetFullscreenState, sc, NULL, &target), S_OK);
    T_TRUE(target == NULL);
    T_EQ(rt_com_refs(out), 1);
}
