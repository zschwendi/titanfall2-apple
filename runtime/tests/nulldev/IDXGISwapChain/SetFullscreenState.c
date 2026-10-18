/* Expected values: Microsoft's page of IDXGISwapChain::SetFullscreenState (read 2026-10-10:
 * S_OK when the state is placed; "If you pass FALSE to Fullscreen, then you must set this
 * parameter to NULL" for pTarget).  The two stops are the contract's choice, not the page's:
 * this swap chain is always windowed (finding 31, section 2), and what TRUE answers on a device
 * with no display is an open decision (docs/handoff/d3d-contract.md).  The target is a real
 * output of the device's adapter, from EnumOutputs.  "Changes nothing" means Get still says
 * FALSE and no reference moves; Get's own values come from the notes, not from this body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGISwapChain, SetFullscreenState)
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

    T_CASE("SetFullscreenState(FALSE, NULL) is S_OK");
    T_EQ(rt_com_refs(sc), 1);
    T_EQ(COM_CALL(IDXGISwapChain, SetFullscreenState, sc, FALSE, NULL), S_OK);

    T_CASE("SetFullscreenState(FALSE, NULL) changes nothing: Get still says FALSE and no reference moves");
    fs = -1;
    target = out;
    T_EQ(COM_CALL(IDXGISwapChain, GetFullscreenState, sc, &fs, &target), S_OK);
    T_EQ(fs, FALSE);
    T_TRUE(target == NULL);
    T_EQ(rt_com_refs(sc), 1);
    T_EQ(rt_com_refs(out), 1);

    T_TRAPS(COM_CALL(IDXGISwapChain, SetFullscreenState, sc, TRUE, NULL), "SetFullscreenState(TRUE)");
    T_TRAPS(COM_CALL(IDXGISwapChain, SetFullscreenState, sc, FALSE, out), "SetFullscreenState(FALSE) with a target output");
}
