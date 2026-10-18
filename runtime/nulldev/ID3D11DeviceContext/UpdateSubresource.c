/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource
 * RT-CASES:  the bytes of a box of a staging texture are read back at the next read mapping, rows SrcRowPitch apart; a buffer takes the bytes of its box as they lie; a box written into a subresource nothing was put in leaves the rest unwritten: a read mapping is a stop until updates have covered every byte; boxes of a texture add up row by row, and a level is the game's only when every texel is; the recording's update of a whole constant buffer of 576 bytes is taken and the buffer holds the game's bytes; an empty box does nothing
 * RT-TRAPS:  UpdateSubresource of a resource created with immutable or dynamic usage; UpdateSubresource with NULL data; outside the destination; UpdateSubresource with SrcRowPitch 8, smaller than a row of 16 bytes; a copy or an update with a NULL destination; a read mapping of a subresource of which calls wrote only a part
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): "The CPU copies data from
 * memory to a subresource created in non-mappable memory."; pDstBox: "Coordinates are in bytes
 * for buffers and in texels for textures.  If NULL, the data is written to the destination
 * subresource with no offset."; "An empty box results in a no-op."; SrcRowPitch: "The size of
 * one row of the source data."; SrcDepthPitch: "The size of one depth slice of source data.";
 * "A resource cannot be used as a destination if: the resource is created with immutable or
 * dynamic usage. the resource is created as a depth-stencil resource. the resource is created
 * with multisampling capability".  Returns nothing.
 *
 * The bytes are the game's own, so the destination holds the game's bytes afterwards
 * (rt_nulldev_update; runtime/nulldev/memory.c, "copies"): copied where the device keeps the
 * resource's bytes, read once and dropped where it does not.  The depth-stencil rule is not
 * checked: such a resource is an output resource, and nothing reads its record.  The game
 * makes 78,678 of these calls in the recorded session, all into DEFAULT resources
 * (docs/trace/d3d-map.md). */
COM_METHOD(ID3D11DeviceContext, void, UpdateSubresource, ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_update(ctx, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);   /* the front's checks */
    /* The seam (rt/d3dback.h, rule 2): the resource crosses as its back object; the box and the
     * game's bytes by pointer, unchanged (valid for this call only). */
    RT_BACK(ctx, UpdateSubresource, c->child.back, rt_d3dback_of(pDstResource), DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}
