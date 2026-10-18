/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getcontainingoutput
 * RT-CASES:  a new output object with one reference, of the device's adapter; two calls give two objects
 * RT-TRAPS:  GetContainingOutput with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-11: "Get the output (the display monitor) that contains the majority
 * of the client area of the target window"; on success "the output interface will be filled
 * and its reference count incremented"; "The output is also owned by the adapter on which
 * the swap chain's device was created".  The page names no result for a NULL ppOutput: a stop.
 *
 * There is one output (design-null-device.md 3.4), so it contains every window: the output
 * of the device's adapter, as IDXGIAdapter1/EnumOutputs.c. */
COM_METHOD(IDXGISwapChain, HRESULT, GetContainingOutput, IDXGIOutput **output)
{
    NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    if (!output) RT_UNIMPLEMENTED(ctx, "GetContainingOutput with a NULL out-pointer");
    NdDevice *d = rt_com_state_of(s->device, &rt_nulldev_device_class);
    *output = rt_nulldev_output_new(d->adapter);
    return S_OK;
}
