/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshaderresources
 * RT-CASES:  a slot never set reads NULL; a Set binds the view and holds one reference to it; a Set over an occupied slot releases the old view; a NULL element unbinds its slot and releases the view; NumViews above 1 fills consecutive slots; slot 127 is the last slot; a count of 0 with a NULL array does nothing; a view bound as a render target binds NULL; a view of a second texture binds as usual while the target is bound; a view of the target binds again once the target is unbound
 * RT-TRAPS:  PSSetShaderResources for slots; PSSetShaderResources with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference each bound view holds; and "If an overlapping resource view is already bound to an
 * output slot, such as a rendertarget, then this API will fill the destination shader resource
 * slot with NULL", the rule rt_nulldev_is_target decides.  The page gives StartSlot the range 0 to
 * D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1 and NumViews the range 0 to that count minus
 * StartSlot, and does not give the count's value: ND_SRV_SLOTS is 128, the header's number.  The
 * page marks ppShaderResourceViews [in, optional]; the NULL-array stop and the NULL-element unbind
 * are the notes' rules, not the page's. */
COM_METHOD(ID3D11DeviceContext, void, PSSetShaderResources, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView const **ppShaderResourceViews)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1, which would pass the check
     * and then write past ps_srv. */
    if ((uint64_t)StartSlot + NumViews > ND_SRV_SLOTS)
        RT_UNIMPLEMENTED(ctx, "PSSetShaderResources for slots %u to %u", StartSlot, StartSlot + NumViews - 1);
    if (NumViews > 0 && !ppShaderResourceViews)
        RT_UNIMPLEMENTED(ctx, "PSSetShaderResources with a NULL array");
    for (UINT i = 0; i < NumViews; i++) {
        /* A view bound for output is not bound for reading: its slot gets NULL. */
        void *v = rt_nulldev_arg(ctx, (void *)ppShaderResourceViews[i], &rt_nulldev_srv_class, "ppShaderResourceViews");
        rt_nulldev_bind(&c->ps_srv[StartSlot + i], rt_nulldev_is_target(ctx, c, v) ? NULL : v);
    }
    /* The seam (rt/d3dback.h, rule 2), after every check above: the array is rebuilt on the stack
     * with each view's back object, NULL for NULL; a NULL array (count 0) crosses as NULL. */
    if (rt_d3dback()) {
        void *back[ND_SRV_SLOTS];
        for (UINT i = 0; i < NumViews; i++) back[i] = rt_d3dback_of((void *)ppShaderResourceViews[i]);
        RT_BACK(ctx, PSSetShaderResources, c->child.back, StartSlot, NumViews, ppShaderResourceViews ? back : NULL);
    }
}
