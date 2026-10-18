/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getdesc
 * RT-CASES:  the description the swap chain was created with (72 bytes written, nothing after them); a NULL description is a stop
 * RT-TRAPS:  GetDesc with a NULL description
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-11: "Get a description of the swap chain" into a DXGI_SWAP_CHAIN_DESC;
 * the page names no result for a NULL pDesc: a stop.  The description is the one passed to
 * CreateSwapChain (ResizeBuffers updates it), kept in the swap chain's record
 * (design-null-device.md, section 6). */
COM_METHOD(IDXGISwapChain, HRESULT, GetDesc, DXGI_SWAP_CHAIN_DESC *desc)
{
    NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    if (!desc) RT_UNIMPLEMENTED(ctx, "GetDesc with a NULL description");
    *desc = s->desc;
    return S_OK;
}
