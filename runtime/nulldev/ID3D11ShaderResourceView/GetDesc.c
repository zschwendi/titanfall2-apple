/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11shaderresourceview-getdesc
 * RT-CASES:  the description the view was made with, all 24 bytes and nothing after them; a view made with a NULL description: the one the creator derived
 * RT-TRAPS:  ID3D11ShaderResourceView::GetDesc with a NULL description
 */
#include <string.h>

#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Get the shader resource view's description."; pDesc: "A pointer to a
 * D3D11_SHADER_RESOURCE_VIEW_DESC structure to be filled with data about the shader resource
 * view."; return value: "None".
 * The game calls this in mission frames (docs/findings/40-d3d-call-trace.md, row Z: 381 calls;
 * it is not one of finding 31's 146 methods).  What it copies out is the view's record,
 * NdView.desc.srv: the description the creator kept (rt_nulldev_view_create), which is the one
 * Windows reads back (docs/reviews/d3d-contract-check-2.md, F1, from Wine's
 * test_create_shader_resource_view).  All 24 bytes of D3D11_SHADER_RESOURCE_VIEW_DESC are
 * written (rt/win/d3d11view.h has the layout).  A NULL pointer: the page names no result, so a
 * stop. */
COM_METHOD(ID3D11ShaderResourceView, void, GetDesc, D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc)
{
    const NdView *v = rt_com_state(ctx, This, &rt_nulldev_srv_class);
    if (!pDesc) RT_UNIMPLEMENTED(ctx, "ID3D11ShaderResourceView::GetDesc with a NULL description");
    memcpy(pDesc, &v->desc.srv, sizeof *pDesc);
}
