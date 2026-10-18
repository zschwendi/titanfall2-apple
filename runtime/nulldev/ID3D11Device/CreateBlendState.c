/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createblendstate
 * RT-CASES:  a new blend state: S_OK, one reference, the device held, the normalised description kept; an equal description gives the same object with one more reference; with IndependentBlendEnable FALSE a description differing only in RenderTarget[3] is the same object; a different description gives another object; after the last Release an equal description gives a new object; a zeroed description with one write mask set (Wine's): S_OK, and all 8 targets hold FALSE, ONE, ZERO, ADD, ONE, ZERO, ADD and the mask; the recording's call 7088, stray numbers in the targets that do not blend: S_OK, those targets hold the defaults and their own mask; descriptions that differ only in the unused fields of a target that does not blend are the same object, and a different mask there is another
 * RT-TRAPS:  a state creator with a NULL description or a NULL out-pointer; a blend factor that is not one of D3D11_BLEND; an alpha blend factor that is not one of D3D11_BLEND or ends in _COLOR; a blend operation that is not one of D3D11_BLEND_OP; a write mask above D3D11_COLOR_WRITE_ENABLE_ALL
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text):
 * "For each object created, the runtime checks to see if a previous object has the same state.  If such a previous
 * object exists, the runtime will return a pointer to previous instance instead of creating a duplicate object."
 * So this is decision D6 of the contract, built as the page says: rt_nulldev_state_create
 * (rt/nulldev.h; runtime/nulldev/creators.c has what "the same state" means, from Wine's
 * conformance tests through docs/reviews/d3d-contract-check.md, E5, and every stop).
 * Only what Windows uses of the description is checked (unit d3d-fix-2; creators.c has the
 * sources): with IndependentBlendEnable FALSE render target 0 alone, and of a target whose
 * BlendEnable is FALSE only its write mask.  The game leaves stray numbers in the rest. */
COM_METHOD(ID3D11Device, HRESULT, CreateBlendState, const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState)
{
    HRESULT hr = rt_nulldev_state_create(ctx, This, &rt_nulldev_blend_class, pBlendStateDesc, (void **)ppBlendState);
    /* The seam (rt/d3dback.h, rules 3 and 7): a state object that existed already has its back
     * object, and the front answers alone; a new one gets its own from the game's description. */
    RT_BACK_CREATE(ctx, *ppBlendState, CreateBlendState, rt_d3dback_of(This), pBlendStateDesc);
    return hr;
}
