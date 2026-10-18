/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createrasterizerstate
 * RT-CASES:  a new rasterizer state: S_OK, one reference, the device held, the description kept; an equal description gives the same object with one more reference; a different description gives another object
 * RT-TRAPS:  a state creator with a NULL description or a NULL out-pointer; a fill mode that is not one of D3D11_FILL_MODE; a cull mode that is not one of D3D11_CULL_MODE
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text):
 * "If an application attempts to create a rasterizer-state interface with the same state as an existing interface,
 * the same interface will be returned and the total number of unique rasterizer state objects will stay the same."
 * So this is decision D6 of the contract, built as the page says: rt_nulldev_state_create
 * (rt/nulldev.h; runtime/nulldev/creators.c has what "the same state" means, from Wine's
 * conformance tests through docs/reviews/d3d-contract-check.md, E5, and every stop). */
COM_METHOD(ID3D11Device, HRESULT, CreateRasterizerState, const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState)
{
    HRESULT hr = rt_nulldev_state_create(ctx, This, &rt_nulldev_rasterizer_class, pRasterizerDesc, (void **)ppRasterizerState);
    /* The seam (rt/d3dback.h, rules 3 and 7): a state object that existed already has its back
     * object, and the front answers alone; a new one gets its own from the game's description. */
    RT_BACK_CREATE(ctx, *ppRasterizerState, CreateRasterizerState, rt_d3dback_of(This), pRasterizerDesc);
    return hr;
}
