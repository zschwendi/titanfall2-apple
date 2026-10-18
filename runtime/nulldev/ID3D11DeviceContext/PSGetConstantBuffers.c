/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-psgetconstantbuffers
 * RT-CASES:  a Get of a never-set slot returns NULL; a Get returns the bound buffer with one more reference; a Get of a cleared slot returns NULL; NumBuffers above 1 returns consecutive slots; slot 13 is the last slot; a count of 0 with a NULL array does nothing
 * RT-TRAPS:  PSGetConstantBuffers for slots; PSGetConstantBuffers with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one", the reference each returned buffer gains for the caller.  The page gives StartSlot the
 * range 0 to D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1 and NumBuffers the range 0 to
 * that count minus StartSlot, and does not give the count's value: ND_CB_SLOTS is 14, from memory.
 * The page marks ppConstantBuffers [out, optional]; the NULL-array stop is the notes' rule, not the
 * page's. */
COM_METHOD(ID3D11DeviceContext, void, PSGetConstantBuffers, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1, which would pass the check
     * and then read past ps_cb. */
    if ((uint64_t)StartSlot + NumBuffers > ND_CB_SLOTS)
        RT_UNIMPLEMENTED(ctx, "PSGetConstantBuffers for slots %u to %u", StartSlot, StartSlot + NumBuffers - 1);
    if (NumBuffers > 0 && !ppConstantBuffers)
        RT_UNIMPLEMENTED(ctx, "PSGetConstantBuffers with a NULL array");
    for (UINT i = 0; i < NumBuffers; i++)
        ppConstantBuffers[i] = rt_nulldev_ref(c->ps_cb[StartSlot + i]);
}
