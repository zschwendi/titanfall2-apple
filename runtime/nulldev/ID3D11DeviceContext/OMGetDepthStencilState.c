/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omgetdepthstencilstate
 * RT-CASES:  a new context reads NULL and stencil reference 0; a Get returns the bound state with one more reference; a NULL out-pointer is skipped; a Get after a NULL Set reads NULL
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented by
 * one", the reference handed out for the bound state.  Each out-pointer that is not NULL receives
 * its value; a NULL out-pointer is skipped and hands out no reference. */
COM_METHOD(ID3D11DeviceContext, void, OMGetDepthStencilState, ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (ppDepthStencilState)
        *ppDepthStencilState = rt_nulldev_ref(c->depth_stencil);
    if (pStencilRef)
        *pStencilRef = c->stencil_ref;
}
