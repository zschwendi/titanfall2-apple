/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vsgetconstantbuffers
 * RT-CASES:  a slot never set reads NULL; a Get returns each bound buffer with one more reference; a Get of the last slot returns it too; a count of 0 with a NULL array does nothing
 * RT-TRAPS:  VSGetConstantBuffers for slots; VSGetConstantBuffers with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one".  The page marks ppConstantBuffers [out, optional]; the NULL-array stop is the notes'
 * rule, not the page's.  The slot range is as in VSSetConstantBuffers: the page gives no value
 * for the slot count, and ND_CB_SLOTS is 14, from memory. */
COM_METHOD(ID3D11DeviceContext, void, VSGetConstantBuffers, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits, as in VSSetConstantBuffers: in UINT, 0xffffffff + 2 wraps. */
    if ((uint64_t)StartSlot + NumBuffers > ND_CB_SLOTS)
        RT_UNIMPLEMENTED(ctx, "VSGetConstantBuffers for slots %u to %u", StartSlot, StartSlot + NumBuffers - 1);
    if (NumBuffers > 0 && !ppConstantBuffers)
        RT_UNIMPLEMENTED(ctx, "VSGetConstantBuffers with a NULL array");
    for (UINT i = 0; i < NumBuffers; i++)
        ppConstantBuffers[i] = rt_nulldev_ref(c->vs_cb[StartSlot + i]);
}
