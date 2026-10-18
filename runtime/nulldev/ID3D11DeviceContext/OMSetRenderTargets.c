/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets
 * RT-CASES:  a slot never set reads NULL; a Set binds the view and holds one reference to it; a Set over an occupied slot releases the old view; a Set of one view after two leaves slot 1 NULL and released; NumViews of 8 fills every slot; a NULL depth-stencil view unbinds it and releases it; a depth-stencil Set binds the view and holds one reference to it; a Set binds a view of a texture being read unbinds the reader; a depth-stencil Set unbinds a reader of its texture
 * RT-TRAPS:  OMSetRenderTargets with; OMSetRenderTargets with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in"; "Any
 * render targets not defined by this call are set to NULL"; and "If any subresources are also
 * currently bound for reading in a different stage or writing ... those bind points will be set to
 * NULL".  The notes add the stops: NumViews above ND_RT_SLOTS (8), and a NULL array with NumViews
 * above 0.  The page's "If this parameter is NULL, the depth-stencil view is not bound" is the
 * notes' rule that a NULL pDepthStencilView unbinds.  The page's NumViews range (0 to 8) is from
 * memory and agrees with ND_RT_SLOTS. */
COM_METHOD(ID3D11DeviceContext, void, OMSetRenderTargets, UINT NumViews, ID3D11RenderTargetView const **ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (NumViews > ND_RT_SLOTS)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargets with %u views", NumViews);
    if (NumViews > 0 && !ppRenderTargetViews)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargets with a NULL array");
    for (UINT i = 0; i < ND_RT_SLOTS; i++) {
        void *view = NULL;
        if (i < NumViews)
            view = rt_nulldev_arg(ctx, (void *)ppRenderTargetViews[i], &rt_nulldev_rtv_class, "ppRenderTargetViews");
        rt_nulldev_bind(&c->rtv[i], view);
    }
    rt_nulldev_bind(&c->dsv, rt_nulldev_arg(ctx, pDepthStencilView, &rt_nulldev_dsv_class, "pDepthStencilView"));
    /* A subresource bound for writing is not also bound for reading: clear the readers. */
    for (UINT i = 0; i < NumViews; i++)
        rt_nulldev_unbind_readers(ctx, c, c->rtv[i]);
    rt_nulldev_unbind_readers(ctx, c, c->dsv);
    /* The seam (rt/d3dback.h, rule 2), after every check above: the views cross as the game passed
     * them (NumViews of them), each as its back object, NULL for NULL, in an array rebuilt on the
     * stack; a NULL array (count 0) crosses as NULL. */
    if (rt_d3dback()) {
        void *back[ND_RT_SLOTS];
        for (UINT i = 0; i < NumViews; i++) back[i] = rt_d3dback_of((void *)ppRenderTargetViews[i]);
        RT_BACK(ctx, OMSetRenderTargets, c->child.back, NumViews, ppRenderTargetViews ? back : NULL, rt_d3dback_of(pDepthStencilView));
    }
}
