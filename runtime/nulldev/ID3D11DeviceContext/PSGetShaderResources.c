/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-psgetshaderresources
 * RT-CASES:  a Get of a slot never set returns NULL; a Get returns the bound view and adds one reference for the caller; a Get of a slot cleared by a NULL element returns NULL; a Get reads several consecutive slots; slot 127 is the last slot; a count of 0 with a NULL array does nothing
 * RT-TRAPS:  PSGetShaderResources for slots; PSGetShaderResources with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one", the reference each returned view gives the caller.  The page gives StartSlot the range
 * 0 to D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1 and NumViews the range 0 to that count
 * minus StartSlot, and does not give the count's value: ND_SRV_SLOTS is 128, the header's number.
 * The page marks ppShaderResourceViews [out, optional]; the NULL-array stop is the notes' rule,
 * not the page's. */
COM_METHOD(ID3D11DeviceContext, void, PSGetShaderResources, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1, which would pass the check
     * and then read past ps_srv. */
    if ((uint64_t)StartSlot + NumViews > ND_SRV_SLOTS)
        RT_UNIMPLEMENTED(ctx, "PSGetShaderResources for slots %u to %u", StartSlot, StartSlot + NumViews - 1);
    if (NumViews > 0 && !ppShaderResourceViews)
        RT_UNIMPLEMENTED(ctx, "PSGetShaderResources with a NULL array");
    for (UINT i = 0; i < NumViews; i++)
        ppShaderResourceViews[i] = rt_nulldev_ref(c->ps_srv[StartSlot + i]);
}
