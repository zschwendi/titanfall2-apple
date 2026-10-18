/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cleardepthstencilview
 * RT-CASES:  with no back end a clear is accepted and nothing happens; with a back end it is handed on once, the view as its back object, the depth and the stencil by value
 * RT-TRAPS:  ClearDepthStencilView of a NULL view; ClearDepthStencilView with flags 0x; rt_nulldev_arg: "is an object of class" ... "where" ... "is expected" (a view of the wrong class)
 */
#include "rt/nulldev.h"

/* A clear of the depth and stencil of a view: the second worked example of the back-end rows
 * (unit back-clear-dsv-uav; rt/d3dback.h).  The flags may name only the depth and the stencil
 * (D3D11_CLEAR_DEPTH, D3D11_CLEAR_STENCIL); anything else is a stop here, not a call the back
 * end must refuse.  The view must be a live depth-stencil view of this layer (rt_nulldev_arg)
 * and not NULL.  Depth and Stencil cross by value, as the game gave them. */
COM_METHOD(ID3D11DeviceContext, void, ClearDepthStencilView, ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pDepthStencilView) RT_UNIMPLEMENTED(ctx, "ClearDepthStencilView of a NULL view");
    if (ClearFlags & ~(D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL)) RT_UNIMPLEMENTED(ctx, "ClearDepthStencilView with flags 0x%x", (unsigned)ClearFlags);
    (void)rt_nulldev_arg(ctx, pDepthStencilView, &rt_nulldev_dsv_class, "pDepthStencilView");
    RT_BACK(ctx, ClearDepthStencilView, c->child.back, rt_d3dback_of(pDepthStencilView), ClearFlags, Depth, Stencil);
}
