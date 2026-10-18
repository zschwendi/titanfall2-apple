/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-setfullscreenstate
 * RT-CASES:  SetFullscreenState(FALSE, NULL) is S_OK; SetFullscreenState(FALSE, NULL) changes nothing: Get still says FALSE and no reference moves
 * RT-TRAPS:  SetFullscreenState(TRUE); SetFullscreenState(FALSE) with a target output
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "If you pass FALSE to Fullscreen, then you must set this parameter
 * to NULL" (pTarget); S_OK when the state is placed.  The page names DXGI_ERROR_NOT_CURRENTLY_
 * AVAILABLE for a switch that fails (Terminal Server, occluded window, another full-screen
 * app) but says nothing about a device with no display.  This swap chain is always windowed
 * (finding 31, section 2), so FALSE with NULL changes nothing; TRUE, and FALSE with a target,
 * are stops: TRUE is the open decision in docs/handoff/d3d-contract.md. */
COM_METHOD(IDXGISwapChain, HRESULT, SetFullscreenState, BOOL fullscreen, IDXGIOutput *target)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    if (fullscreen) RT_UNIMPLEMENTED(ctx, "SetFullscreenState(TRUE)");
    if (target) RT_UNIMPLEMENTED(ctx, "SetFullscreenState(FALSE) with a target output");
    return S_OK;
}
