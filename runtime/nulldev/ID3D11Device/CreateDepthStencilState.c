/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilstate
 * RT-CASES:  a new depth-stencil state: S_OK, one reference, the device held, the normalised description kept; an equal description gives the same object with one more reference; with depth and stencil disabled a description differing only in unused fields is the same object; a different description gives another object
 * RT-TRAPS:  a state creator with a NULL description or a NULL out-pointer; a depth function that is not one of D3D11_COMPARISON_FUNC
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text):
 * "If an application attempts to create a depth-stencil-state interface with the same state as an existing
 * interface, the same interface will be returned and the total number of unique depth-stencil state objects will stay
 * the same."
 * So this is decision D6 of the contract, built as the page says: rt_nulldev_state_create
 * (rt/nulldev.h; runtime/nulldev/creators.c has what "the same state" means, from Wine's
 * conformance tests through docs/reviews/d3d-contract-check.md, E5, and every stop). */
COM_METHOD(ID3D11Device, HRESULT, CreateDepthStencilState, const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState)
{
    HRESULT hr = rt_nulldev_state_create(ctx, This, &rt_nulldev_depthstencil_class, pDepthStencilDesc, (void **)ppDepthStencilState);
    /* The seam (rt/d3dback.h, rules 3 and 7): a state object that existed already has its back
     * object, and the front answers alone; a new one gets its own from the game's description. */
    RT_BACK_CREATE(ctx, *ppDepthStencilState, CreateDepthStencilState, rt_d3dback_of(This), pDepthStencilDesc);
    return hr;
}
