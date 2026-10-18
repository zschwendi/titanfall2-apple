/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetblendstate
 * RT-CASES:  a new context binds no blend state; a Set binds the blend state and holds one reference to it; setting the bound state again keeps its count; a Set over a bound state releases the old one; a blend factor is stored as given; a NULL blend factor stores {1, 1, 1, 1}; a sample mask is stored as given; a NULL blend state unbinds and releases the old one
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference the bound blend state holds.  "If you pass NULL, the runtime uses or stores a blend
 * factor equal to { 1, 1, 1, 1 }" gives the NULL blend factor.  The page's "Pass NULL for a default
 * blend state" is read as unbinding, as the notes say: the record keeps NULL for it.  The sample
 * mask is stored as given. */
COM_METHOD(ID3D11DeviceContext, void, OMSetBlendState, ID3D11BlendState *pBlendState, const FLOAT *BlendFactor, UINT SampleMask)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_bind(&c->blend, rt_nulldev_arg(ctx, pBlendState, &rt_nulldev_blend_class, "pBlendState"));
    for (UINT i = 0; i < 4; i++)
        c->blend_factor[i] = BlendFactor ? BlendFactor[i] : 1.0f;
    c->sample_mask = SampleMask;
    /* The seam (rt/d3dback.h, rule 2): the state crosses as its back object, NULL as NULL; the factor
     * and the mask cross as the game passed them. */
    RT_BACK(ctx, OMSetBlendState, c->child.back, rt_d3dback_of(pBlendState), BlendFactor, SampleMask);
}
