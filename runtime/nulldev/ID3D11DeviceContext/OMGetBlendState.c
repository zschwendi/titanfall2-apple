/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omgetblendstate
 * RT-CASES:  a new context reads NULL, {1, 1, 1, 1} and 0xffffffff; a Get returns the bound state with one more reference; a Get returns the stored blend factor and sample mask; a NULL out-pointer is skipped and the others are still written
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: the page says "The reference count of the returned interface will be
 * incremented by one when the blend state is retrieved", which is the rule the notes give in other
 * words (their quotation, "Any returned interfaces will have their reference count incremented by
 * one", is not on the page).  Each out parameter is marked [out, optional], so a NULL is skipped.
 * The values written are the record's: the notes' rule, with the same new-context defaults as the
 * OMSetBlendState page. */
COM_METHOD(ID3D11DeviceContext, void, OMGetBlendState, ID3D11BlendState **ppBlendState, FLOAT *BlendFactor, UINT *pSampleMask)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (ppBlendState)
        *ppBlendState = rt_nulldev_ref(c->blend);
    if (BlendFactor)
        for (UINT i = 0; i < 4; i++)
            BlendFactor[i] = c->blend_factor[i];
    if (pSampleMask)
        *pSampleMask = c->sample_mask;
}
