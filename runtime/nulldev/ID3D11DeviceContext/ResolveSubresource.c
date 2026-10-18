/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-resolvesubresource
 * RT-CASES:  a resolve of a multisampled texture leaves the destination subresource a renderer's, and no other
 * RT-TRAPS:  ResolveSubresource with a NULL resource; ResolveSubresource from a source that is not multisampled; ResolveSubresource into a destination that is multisampled; ResolveSubresource of subresource
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): "Copy a multisampled resource
 * into a non-multisampled resource."; pSrcResource: "Source resource. Must be multisampled.";
 * pDstResource: "Destination resource. Must be a created with the D3D11_USAGE_DEFAULT flag and
 * be single-sampled."  Returns nothing.
 *
 * A resolve computes each element from several samples: only a renderer can produce the
 * result, and the destination subresource is recorded so (rt_nulldev_rendered; rt/nulldev.h,
 * ND_CONTENT_*).  Format is not looked at: nothing is computed.  The recorded session never
 * calls this method (no texture in it is multisampled: docs/trace/d3d-creators.md); finding
 * 31 found the call in the code. */
COM_METHOD(ID3D11DeviceContext, void, ResolveSubresource, ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pDstResource || !pSrcResource) RT_UNIMPLEMENTED(ctx, "ResolveSubresource with a NULL resource (%p, %p)", (void *)pDstResource, (void *)pSrcResource);
    pDstResource = rt_nulldev_resource_arg(ctx, pDstResource, "pDstResource");
    pSrcResource = rt_nulldev_resource_arg(ctx, pSrcResource, "pSrcResource");
    const NdMemory *md = rt_nulldev_memory(pDstResource), *ms = rt_nulldev_memory(pSrcResource);
    if (ms->size) RT_UNIMPLEMENTED(ctx, "ResolveSubresource from a source that is not multisampled");
    if (!md->size) RT_UNIMPLEMENTED(ctx, "ResolveSubresource into a destination that is multisampled");
    if (DstSubresource >= md->nsub || SrcSubresource >= ms->nsub)
        RT_UNIMPLEMENTED(ctx, "ResolveSubresource of subresource %u of %u into %u of %u", (unsigned)SrcSubresource, (unsigned)ms->nsub,
                         (unsigned)DstSubresource, (unsigned)md->nsub);
    rt_nulldev_rendered(pDstResource, DstSubresource);
    RT_BACK(ctx, ResolveSubresource, c->child.back, rt_d3dback_of(pDstResource), DstSubresource, rt_d3dback_of(pSrcResource), SrcSubresource, Format);
}
