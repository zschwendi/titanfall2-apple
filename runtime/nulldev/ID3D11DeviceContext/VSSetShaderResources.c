/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetshaderresources
 * RT-CASES:  three slots with a NULL in the middle cross as back objects in an array that is not the game's; a count of 0 with a NULL array crosses as NULL; slot 127 is the last slot
 * RT-TRAPS:  VSSetShaderResources for slots; VSSetShaderResources with a NULL array; is an object of class
 */
#include "rt/nulldev.h"

COM_METHOD(ID3D11DeviceContext, void, VSSetShaderResources, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView const **ppShaderResourceViews)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    /* The sum is taken in 64 bits: in UINT, 0xffffffff + 2 wraps to 1, which would pass the check. */
    if ((uint64_t)StartSlot + NumViews > ND_SRV_SLOTS)
        RT_UNIMPLEMENTED(ctx, "VSSetShaderResources for slots %u to %u", StartSlot, StartSlot + NumViews - 1);
    if (NumViews > 0 && !ppShaderResourceViews)
        RT_UNIMPLEMENTED(ctx, "VSSetShaderResources with a NULL array");
    for (UINT i = 0; i < NumViews; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppShaderResourceViews[i], &rt_nulldev_srv_class, "ppShaderResourceViews");
    /* The seam (rt/d3dback.h, rule 2), after every check above: the array is rebuilt on the stack
     * with each view's back object, NULL for NULL; a NULL array (count 0) crosses as NULL. */
    if (rt_d3dback()) {
        void *back[ND_SRV_SLOTS];
        for (UINT i = 0; i < NumViews; i++) back[i] = rt_d3dback_of((void *)ppShaderResourceViews[i]);
        RT_BACK(ctx, VSSetShaderResources, c->child.back, StartSlot, NumViews, ppShaderResourceViews ? back : NULL);
    }
}
