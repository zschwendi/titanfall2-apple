/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion
 * RT-CASES:  a box of a staging buffer the game wrote is copied to its offset in another, and the rest stays; a block of a block-compressed staging texture is copied to another block; an empty box does nothing; a copy from a render target leaves the destination a renderer's, also when it covers a part; a copy from a subresource nothing was put in gives zeroes and no contents to read; a box copied out of the written part of such a subresource is the game's, and a box that touches the unwritten part is not; a copy of all of a subresource nothing was put in leaves nothing to read; the recording's copy of mip k to mip k + 1 of another texture is taken
 * RT-TRAPS:  a copy or an update with a NULL; a copy or an update of a subresource the; outside the source; which Microsoft calls undefined; does not lie on the grid; a copy between formats whose elements differ in size; a copy between resources of different type; onto itself; a copy into an immutable resource; a read mapping of a subresource that a copy from a renderer's output or a resolve wrote; a read mapping of a subresource of which calls wrote only a part; a read mapping of a subresource no call wrote
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): "Copy a region from a source
 * resource to a destination resource."; pSrcBox: "If NULL, the entire source subresource is
 * copied."; "An empty box results in a no-op."; "If the resources are buffers, all coordinates
 * are in bytes; if the resources are textures, all coordinates are in texels."; "If you try and
 * copy outside the destination resource or specify a source box that is larger than the source
 * resource, the behavior of CopySubresourceRegion is undefined."  Returns nothing.
 *
 * The device draws nothing, so a copy is one of two things (unit d3d-fix-2; runtime/nulldev/
 * memory.c, "copies", has the rules, the sources and every stop): a memory copy, when the
 * source's bytes are the game's and the device holds them and the destination keeps bytes; or
 * a note on the destination subresource of what it would now hold (a renderer's output, the
 * game's bytes that were not kept, nothing), which ID3D11DeviceContext::Map reads before it
 * hands out memory for reading.  Until this body the method was an accept line, and a staging
 * resource the game wrote and later copied into read back its old bytes. */
COM_METHOD(ID3D11DeviceContext, void, CopySubresourceRegion, ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_copy(ctx, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
    RT_BACK(ctx, CopySubresourceRegion, c->child.back, rt_d3dback_of(pDstResource), DstSubresource, DstX, DstY, DstZ,
            rt_d3dback_of(pSrcResource), SrcSubresource, pSrcBox);
}
