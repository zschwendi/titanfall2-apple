/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-unmap
 * RT-CASES:  after Unmap the subresource can be mapped again and the count of mapped subresources is 0; the bytes stay where they were
 * RT-TRAPS:  Unmap of a NULL resource; Unmap of a subresource the resource does not have; Unmap of a subresource that is not mapped
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text): "Invalidate the pointer to a
 * resource and reenable the GPU's access to that resource."  Here it ends the mapping Map
 * recorded; the block and its bytes stay (they are what a later mapping finds).  The pointer
 * is not made unusable: a write through it after Unmap is not caught.  The page says nothing
 * of a subresource that is not mapped or does not exist, nor of a NULL resource: stops (from
 * memory, Windows ignores an Unmap without a Map). */
COM_METHOD(ID3D11DeviceContext, void, Unmap, ID3D11Resource *pResource, UINT Subresource)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pResource) RT_UNIMPLEMENTED(ctx, "Unmap of a NULL resource");
    pResource = rt_nulldev_resource_arg(ctx, pResource, "pResource");
    NdMemory *m = rt_nulldev_memory(pResource);
    /* The seam (rt/d3dback.h, rule 5): with a back end the mapping is the back end's, and the
     * front's record of it is NdMemory.back_mapped; the same two stops, then the call is handed on. */
    if (rt_d3dback()) {
        if (Subresource >= m->nsub || !m->back_mapped)
            RT_UNIMPLEMENTED(ctx, "Unmap of a subresource the resource does not have: %u of %u", (unsigned)Subresource, (unsigned)m->nsub);
        if (!m->back_mapped[Subresource]) RT_UNIMPLEMENTED(ctx, "Unmap of a subresource that is not mapped (%u)", (unsigned)Subresource);
        m->back_mapped[Subresource] = 0;
        m->mapped--;
        RT_BACK(ctx, Unmap, c->child.back, rt_d3dback_of(pResource), Subresource);
        return;
    }
    if (Subresource >= m->nsub || !m->blocks)
        RT_UNIMPLEMENTED(ctx, "Unmap of a subresource the resource does not have: %u of %u", (unsigned)Subresource, (unsigned)m->nsub);
    if (!m->blocks[Subresource].map_type) RT_UNIMPLEMENTED(ctx, "Unmap of a subresource that is not mapped (%u)", (unsigned)Subresource);
    m->blocks[Subresource].map_type = 0;
    m->mapped--;
}
