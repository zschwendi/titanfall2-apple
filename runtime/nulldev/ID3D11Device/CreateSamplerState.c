/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createsamplerstate
 * RT-CASES:  the game's first sampler: S_OK, one reference, the device held; an equal description gives the same object with one more reference; a linear, WRAP sampler is normalised: MaxAnisotropy 0, ComparisonFunc NEVER, BorderColor zeros, so MaxAnisotropy 16 and 1 are the same object; a different description gives another object; after the last Release an equal description gives a new object
 * RT-TRAPS:  a state creator with a NULL description or a NULL out-pointer; a filter that is not one of D3D11_FILTER; an address mode that is not one of D3D11_TEXTURE_ADDRESS_MODE; a maximum anisotropy above 16
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 (by a subagent that returned the text):
 * "If an application attempts to create a sampler-state interface with the same state as an existing interface, the
 * same interface will be returned and the total number of unique sampler state objects will stay the same."
 * So this is decision D6 of the contract, built as the page says: rt_nulldev_state_create
 * (rt/nulldev.h; runtime/nulldev/creators.c has what "the same state" means, from Wine's
 * conformance tests through docs/reviews/d3d-contract-check.md, E5, and every stop). */
COM_METHOD(ID3D11Device, HRESULT, CreateSamplerState, const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState)
{
    HRESULT hr = rt_nulldev_state_create(ctx, This, &rt_nulldev_sampler_class, pSamplerDesc, (void **)ppSamplerState);
    /* The seam (rt/d3dback.h, rules 3 and 7): a state object that existed already has its back
     * object, and the front answers alone; a new one gets its own from the game's description. */
    RT_BACK_CREATE(ctx, *ppSamplerState, CreateSamplerState, rt_d3dback_of(This), pSamplerDesc);
    return hr;
}
