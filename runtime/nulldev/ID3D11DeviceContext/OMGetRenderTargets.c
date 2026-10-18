/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omgetrendertargets
 * RT-CASES:  a Get of a slot never set returns NULL; a Get returns the bound view and adds one reference; a Get of the depth-stencil view returns it and adds one reference; a Get with NULL pointers changes no count; a Get of fewer than 8 views leaves the rest of the array untouched
 * RT-TRAPS:  OMGetRenderTargets with
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented by
 * one"; each pointer may be NULL when that part is not wanted.  The stop, NumViews above ND_RT_SLOTS
 * (8), is the notes' rule; the page's range for NumViews (0 to 8) is from memory and agrees. */
COM_METHOD(ID3D11DeviceContext, void, OMGetRenderTargets, UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (NumViews > ND_RT_SLOTS)
        RT_UNIMPLEMENTED(ctx, "OMGetRenderTargets with %u views", NumViews);
    if (ppRenderTargetViews)
        for (UINT i = 0; i < NumViews; i++)
            ppRenderTargetViews[i] = rt_nulldev_ref(c->rtv[i]);
    if (ppDepthStencilView)
        *ppDepthStencilView = rt_nulldev_ref(c->dsv);
}
