/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-gssetconstantbuffers
 * RT-CASES:  a range past slot 13 stops; a NULL array with a count stops; an object of the wrong class stops; three slots with a NULL in the middle cross as back objects; a count of 0 with a NULL array crosses with a NULL array; slot 13 is the last slot
 * RT-TRAPS:  GSSetConstantBuffers for slots; GSSetConstantBuffers with a NULL array; is an object of class
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: the page gives StartSlot the range 0 to
 * D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1 and NumBuffers the range 0 to that count
 * minus StartSlot, and does not give the count's value: ND_CB_SLOTS is 14, from memory.  The page
 * marks ppConstantBuffers [in, optional]; the NULL-array stop is the notes' rule, not the page's. */
COM_METHOD(ID3D11DeviceContext, void, GSSetConstantBuffers, UINT StartSlot, UINT NumBuffers, ID3D11Buffer const **ppConstantBuffers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1. */
    if ((uint64_t)StartSlot + NumBuffers > ND_CB_SLOTS)
        RT_UNIMPLEMENTED(ctx, "GSSetConstantBuffers for slots %u to %u", StartSlot, StartSlot + NumBuffers - 1);
    if (NumBuffers > 0 && !ppConstantBuffers)
        RT_UNIMPLEMENTED(ctx, "GSSetConstantBuffers with a NULL array");
    for (UINT i = 0; i < NumBuffers; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppConstantBuffers[i], &rt_nulldev_buffer_class, "ppConstantBuffers");
    /* The seam (rt/d3dback.h, rule 2), after every check above: the array is rebuilt on the stack
     * with each buffer's back object, NULL for NULL; a NULL array (count 0) crosses as NULL. */
    if (rt_d3dback()) {
        void *back[ND_CB_SLOTS];
        for (UINT i = 0; i < NumBuffers; i++) back[i] = rt_d3dback_of((void *)ppConstantBuffers[i]);
        RT_BACK(ctx, GSSetConstantBuffers, c->child.back, StartSlot, NumBuffers, ppConstantBuffers ? back : NULL);
    }
}
