/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetvertexbuffers
 * RT-CASES:  three slots with a NULL in the middle hand each buffer on as its back object, the strides and offsets by pointer; a count of 0 with NULL arrays hands on NULLs; slot 31 is the last slot; a range past slot 31 is a stop; a NULL buffer array with a count is a stop; NULL strides with a count is a stop; an object of the wrong class is a stop
 * RT-TRAPS:  IASetVertexBuffers for slots; IASetVertexBuffers with a NULL array; an object of class
 */
#include "rt/nulldev.h"

COM_METHOD(ID3D11DeviceContext, void, IASetVertexBuffers, UINT StartSlot, UINT NumBuffers, ID3D11Buffer const **ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if ((uint64_t)StartSlot + NumBuffers > D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
        RT_UNIMPLEMENTED(ctx, "IASetVertexBuffers for slots %u to %u", StartSlot, StartSlot + NumBuffers - 1);
    if (NumBuffers > 0 && (!ppVertexBuffers || !pStrides || !pOffsets))
        RT_UNIMPLEMENTED(ctx, "IASetVertexBuffers with a NULL array (buffers %p, strides %p, offsets %p)",
                         (const void *)ppVertexBuffers, (const void *)pStrides, (const void *)pOffsets);
    for (UINT i = 0; i < NumBuffers; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppVertexBuffers[i], &rt_nulldev_buffer_class, "ppVertexBuffers");
    if (rt_d3dback()) {
        void *back[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        for (UINT i = 0; i < NumBuffers; i++) back[i] = rt_d3dback_of((void *)ppVertexBuffers[i]);
        RT_BACK(ctx, IASetVertexBuffers, c->child.back, StartSlot, NumBuffers, ppVertexBuffers ? back : NULL, pStrides, pOffsets);
    }
}
