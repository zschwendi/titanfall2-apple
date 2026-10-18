/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetdepthstencilstate
 * RT-CASES:  a slot never set reads NULL; a Set binds the state and holds one reference to it; a Set stores the stencil reference; a Set over a bound state releases the old state; a NULL state unbinds and releases the old state; a Set leaves the other bindings alone
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference the bound state holds.  The page also says that NULL selects the default state of
 * D3D11_DEPTH_STENCIL_DESC; the notes bind NULL as NULL (nothing bound), so a Get after a NULL Set
 * reads NULL.  That difference from the page is reported, not written here.  The stencil reference
 * is stored as given. */
COM_METHOD(ID3D11DeviceContext, void, OMSetDepthStencilState, ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_bind(&c->depth_stencil,
                    rt_nulldev_arg(ctx, pDepthStencilState, &rt_nulldev_depthstencil_class, "pDepthStencilState"));
    c->stencil_ref = StencilRef;
    /* The seam (rt/d3dback.h, rule 2): the state crosses as its back object, NULL as NULL. */
    RT_BACK(ctx, OMSetDepthStencilState, c->child.back, rt_d3dback_of(pDepthStencilState), StencilRef);
}
