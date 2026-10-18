/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetsamplers
 * RT-CASES:  a slot never set reads NULL; a Set binds the sampler and holds one reference to it; a Set over an occupied slot releases the old sampler; a NULL element unbinds its slot and releases the sampler; NumSamplers above 1 fills consecutive slots; slot 15 is the last slot; a count of 0 with a NULL array does nothing
 * RT-TRAPS:  PSSetSamplers for slots; PSSetSamplers with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference each bound sampler holds.  The page gives StartSlot the range 0 to
 * D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1 and NumSamplers the range 0 to that count minus
 * StartSlot; ND_SAMPLER_SLOTS is that count, 16 (the header's number).  The page marks
 * ppSamplers [in, optional]; the NULL-array stop and the NULL-element unbind are the notes' rules,
 * not the page's. */
COM_METHOD(ID3D11DeviceContext, void, PSSetSamplers, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState const **ppSamplers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1, which would pass the check
     * and then write past ps_sampler. */
    if ((uint64_t)StartSlot + NumSamplers > ND_SAMPLER_SLOTS)
        RT_UNIMPLEMENTED(ctx, "PSSetSamplers for slots %u to %u", StartSlot, StartSlot + NumSamplers - 1);
    if (NumSamplers > 0 && !ppSamplers)
        RT_UNIMPLEMENTED(ctx, "PSSetSamplers with a NULL array");
    for (UINT i = 0; i < NumSamplers; i++)
        rt_nulldev_bind(&c->ps_sampler[StartSlot + i],
                        rt_nulldev_arg(ctx, (void *)ppSamplers[i], &rt_nulldev_sampler_class, "ppSamplers"));
    /* The seam (rt/d3dback.h, rule 2), after every check above: the array is rebuilt on the stack
     * with each sampler's back object, NULL for NULL; a NULL array (count 0) crosses as NULL. */
    if (rt_d3dback()) {
        void *back[ND_SAMPLER_SLOTS];
        for (UINT i = 0; i < NumSamplers; i++) back[i] = rt_d3dback_of((void *)ppSamplers[i]);
        RT_BACK(ctx, PSSetSamplers, c->child.back, StartSlot, NumSamplers, ppSamplers ? back : NULL);
    }
}
