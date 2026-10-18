/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetsamplers
 * RT-CASES:  three slots with a NULL in the middle are handed on with each sampler's back object; a count of 0 with a NULL array is handed on with a NULL array; slot 15 is the last slot; the call is accepted and nothing is recorded in NdContext
 * RT-TRAPS:  VSSetSamplers for slots; VSSetSamplers with a NULL array; is an object of class
 */
#include "rt/nulldev.h"

/* The ranges and the reference are the page's, as PSSetSamplers.c records them: StartSlot and
 * NumSamplers within ND_SAMPLER_SLOTS (16), and the page's note that the method holds a
 * reference to the interfaces passed in.  The NULL-array stop is the notes' rule, not the page's. */
COM_METHOD(ID3D11DeviceContext, void, VSSetSamplers, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState const **ppSamplers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1, which would pass the check
     * and then go past the sampler slots. */
    if ((uint64_t)StartSlot + NumSamplers > ND_SAMPLER_SLOTS)
        RT_UNIMPLEMENTED(ctx, "VSSetSamplers for slots %u to %u", StartSlot, StartSlot + NumSamplers - 1);
    if (NumSamplers > 0 && !ppSamplers)
        RT_UNIMPLEMENTED(ctx, "VSSetSamplers with a NULL array");
    for (UINT i = 0; i < NumSamplers; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppSamplers[i], &rt_nulldev_sampler_class, "ppSamplers");
    /* The seam (rt/d3dback.h, rule 2), after every check above: the array is rebuilt on the stack
     * with each sampler's back object, NULL for NULL; a NULL array (count 0) crosses as NULL. */
    if (rt_d3dback()) {
        void *back[ND_SAMPLER_SLOTS];
        for (UINT i = 0; i < NumSamplers; i++) back[i] = rt_d3dback_of((void *)ppSamplers[i]);
        RT_BACK(ctx, VSSetSamplers, c->child.back, StartSlot, NumSamplers, ppSamplers ? back : NULL);
    }
}
