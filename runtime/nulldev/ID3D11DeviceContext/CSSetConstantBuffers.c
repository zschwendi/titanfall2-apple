/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cssetconstantbuffers
 * RT-CASES:  three slots with a NULL in the middle cross as back objects in an array that is not the game's; a count of 0 with a NULL array crosses with a NULL array; slot 13 is the last slot
 * RT-TRAPS:  CSSetConstantBuffers for slots; CSSetConstantBuffers with a NULL array; an object of class
 */
#include "rt/nulldev.h"

/* The seam (rt/d3dback.h, rule 2), after every check below: the array is rebuilt on the stack
 * with each buffer's back object, NULL for NULL; a NULL array (count 0) crosses as NULL. */
COM_METHOD(ID3D11DeviceContext, void, CSSetConstantBuffers, UINT StartSlot, UINT NumBuffers, ID3D11Buffer const **ppConstantBuffers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1. */
    if ((uint64_t)StartSlot + NumBuffers > ND_CB_SLOTS)
        RT_UNIMPLEMENTED(ctx, "CSSetConstantBuffers for slots %u to %u", StartSlot, StartSlot + NumBuffers - 1);
    if (NumBuffers > 0 && !ppConstantBuffers)
        RT_UNIMPLEMENTED(ctx, "CSSetConstantBuffers with a NULL array");
    for (UINT i = 0; i < NumBuffers; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppConstantBuffers[i], &rt_nulldev_buffer_class, "ppConstantBuffers");
    if (rt_d3dback()) {
        void *back[ND_CB_SLOTS];
        for (UINT i = 0; i < NumBuffers; i++) back[i] = rt_d3dback_of((void *)ppConstantBuffers[i]);
        RT_BACK(ctx, CSSetConstantBuffers, c->child.back, StartSlot, NumBuffers, ppConstantBuffers ? back : NULL);
    }
}
