/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11rendertargetview-getdesc
 * RT-CASES:  the description the view was made with, all 20 bytes and nothing after them; a view made with a NULL description: the one the creator derived
 * RT-TRAPS:  ID3D11RenderTargetView::GetDesc with a NULL description
 */
#include <string.h>

#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Get the properties of a render target view."; pDesc: "Pointer to the
 * description of a render target view"; return value: "None".
 * The game calls this from the first mission frame on (docs/findings/40-d3d-call-trace.md, row X:
 * 973 calls; it is not one of finding 31's 146 methods).  What it copies out is the view's
 * record, NdView.desc.rtv: the description the creator kept (rt_nulldev_view_create), which is
 * the one Windows reads back (docs/reviews/d3d-contract-check-2.md, F1, from Wine's
 * test_create_rendertarget_view).  All 20 bytes of D3D11_RENDER_TARGET_VIEW_DESC are written
 * (rt/win/d3d11view.h has the layout).  A NULL pointer: the page names no result, so a stop. */
COM_METHOD(ID3D11RenderTargetView, void, GetDesc, D3D11_RENDER_TARGET_VIEW_DESC *pDesc)
{
    const NdView *v = rt_com_state(ctx, This, &rt_nulldev_rtv_class);
    if (!pDesc) RT_UNIMPLEMENTED(ctx, "ID3D11RenderTargetView::GetDesc with a NULL description");
    memcpy(pDesc, &v->desc.rtv, sizeof *pDesc);
}
