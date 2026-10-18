/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-gssetsamplers
 * RT-CASES:  three slots with a NULL in the middle are handed on as back objects in a new array; a count of 0 with a NULL array is handed on with a NULL array; slot 15 is the last slot
 * RT-TRAPS:  GSSetSamplers for slots; GSSetSamplers with a NULL array; is an object of class
 */
#include "rt/nulldev.h"

/* The seam (rt/d3dback.h, rule 2): the call is checked, then handed on with each sampler's back
 * object, NULL as NULL, in an array rebuilt on the stack.  Nothing is recorded in NdContext: no
 * Get of this bind exists. */
COM_METHOD(ID3D11DeviceContext, void, GSSetSamplers, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState const **ppSamplers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1. */
    if ((uint64_t)StartSlot + NumSamplers > ND_SAMPLER_SLOTS)
        RT_UNIMPLEMENTED(ctx, "GSSetSamplers for slots %u to %u", StartSlot, StartSlot + NumSamplers - 1);
    if (NumSamplers > 0 && !ppSamplers)
        RT_UNIMPLEMENTED(ctx, "GSSetSamplers with a NULL array");
    for (UINT i = 0; i < NumSamplers; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppSamplers[i], &rt_nulldev_sampler_class, "ppSamplers");
    if (rt_d3dback()) {
        void *back[ND_SAMPLER_SLOTS];
        for (UINT i = 0; i < NumSamplers; i++) back[i] = rt_d3dback_of((void *)ppSamplers[i]);
        RT_BACK(ctx, GSSetSamplers, c->child.back, StartSlot, NumSamplers, ppSamplers ? back : NULL);
    }
}
