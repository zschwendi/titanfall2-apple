/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-psgetsamplers
 * RT-CASES:  a Get returns the bound sampler with one more reference; a Get of a slot never set returns NULL; a Get of a range returns each slot in order
 * RT-TRAPS:  PSGetSamplers for slots; PSGetSamplers with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one", the reference each returned sampler gets for the caller.  The page gives StartSlot the
 * range 0 to D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1 and NumSamplers the range 0 to that count
 * minus StartSlot; ND_SAMPLER_SLOTS is that count, 16 (the header's number).  The NULL-array stop
 * is the notes' rule, not the page's. */
COM_METHOD(ID3D11DeviceContext, void, PSGetSamplers, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1, which would pass the check
     * and then read past ps_sampler. */
    if ((uint64_t)StartSlot + NumSamplers > ND_SAMPLER_SLOTS)
        RT_UNIMPLEMENTED(ctx, "PSGetSamplers for slots %u to %u", StartSlot, StartSlot + NumSamplers - 1);
    if (NumSamplers > 0 && !ppSamplers)
        RT_UNIMPLEMENTED(ctx, "PSGetSamplers with a NULL array");
    for (UINT i = 0; i < NumSamplers; i++)
        ppSamplers[i] = rt_nulldev_ref(c->ps_sampler[StartSlot + i]);
}
