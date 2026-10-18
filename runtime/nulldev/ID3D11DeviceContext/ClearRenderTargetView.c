/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearrendertargetview
 * RT-CASES:  with no back end a clear is accepted and nothing happens; with a back end it is handed on once, the view as its back object and the colour by pointer
 * RT-TRAPS:  ClearRenderTargetView of a NULL view; ClearRenderTargetView with a NULL colour
 */
#include "rt/nulldev.h"

/* A clear: the worked example of its shape for the back-end rows (unit pic-seam; rt/d3dback.h).
 * The device writes no contents (docs/handoff/d3d-contract.md, section 7); until this unit the
 * method was a line of runtime/com/accept.def and checked nothing.  A renderer must be able to
 * trust what it is handed, so the front now checks, with no back end as well: the view is a
 * live render-target view of this layer (rt_nulldev_arg) and not NULL, and the colour is not
 * NULL.  The page (RT-DOC; not read in this unit, from memory) gives both parameters as [in]
 * with no word on NULL; Direct3D reads four floats through the pointer.  Both are stops, not
 * returns: a NULL here means something upstream is already wrong. */
COM_METHOD(ID3D11DeviceContext, void, ClearRenderTargetView, ID3D11RenderTargetView *pRenderTargetView, const FLOAT *ColorRGBA)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pRenderTargetView) RT_UNIMPLEMENTED(ctx, "ClearRenderTargetView of a NULL view");
    if (!ColorRGBA) RT_UNIMPLEMENTED(ctx, "ClearRenderTargetView with a NULL colour");
    (void)rt_nulldev_arg(ctx, pRenderTargetView, &rt_nulldev_rtv_class, "pRenderTargetView");
    /* The seam (rt/d3dback.h, rule 2), after the checks: the view crosses as its back object, the
     * four floats by pointer, unchanged. */
    RT_BACK(ctx, ClearRenderTargetView, c->child.back, rt_d3dback_of(pRenderTargetView), ColorRGBA);
}
