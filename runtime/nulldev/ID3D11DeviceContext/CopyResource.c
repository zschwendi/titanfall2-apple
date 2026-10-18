/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
 * RT-CASES:  a staging texture the game wrote is copied whole, every mip level, into another; the recording's read-back, a render target copied into a 32 by 32 staging texture: the copy is taken and the staging texture is a renderer's; a later copy of the game's bytes over all of it makes it readable again; formats of one type group are copied, as bytes
 * RT-TRAPS:  CopyResource of a resource onto itself; CopyResource between resources of 3 and 1 subresources; CopyResource between resources of different dimensions; a copy between resources of different type; a read mapping of a subresource that a copy from a renderer's output or a resolve wrote; a copy between formats of different type groups; a copy into a resource that is currently mapped; a copy from a resource that is currently mapped
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): "Copy the entire contents of
 * the source resource to the destination resource using the GPU."; the two "Must be different
 * resources.", "Must be the same type.", "Must have identical dimensions (including width,
 * height, depth, and size as appropriate).", "Must have compatible DXGI formats, which means
 * the formats must be identical or at least from the same type group."; "You can't use an
 * Immutable resource as a destination."  Returns nothing.
 *
 * Every subresource is copied as ID3D11DeviceContext::CopySubresourceRegion copies one whole
 * (rt_nulldev_copy_all; runtime/nulldev/memory.c, "copies").  The recording's only read-back
 * is made with this method: a render target of R32_TYPELESS into a 32 by 32 staging texture of
 * R32_FLOAT, 381 times (docs/trace/d3d-map.md); the staging texture then holds what only a
 * renderer could have produced, and its read mapping is the stop (or the switch) of Map. */
COM_METHOD(ID3D11DeviceContext, void, CopyResource, ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_copy_all(ctx, pDstResource, pSrcResource);       /* every check and stop of the front is in there */
    /* The seam (rt/d3dback.h, rule 2): both resources cross as their back objects. */
    RT_BACK(ctx, CopyResource, c->child.back, rt_d3dback_of(pDstResource), rt_d3dback_of(pSrcResource));
}
