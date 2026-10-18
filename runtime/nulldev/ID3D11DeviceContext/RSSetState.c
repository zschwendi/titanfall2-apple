/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetstate
 * RT-CASES:  a slot never set reads NULL; a Set binds the rasterizer state and holds one reference to it; a Set over an occupied slot releases the old state; a NULL argument unbinds and releases the state
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference the bound rasterizer state holds.  The page gives no NULL rule; a NULL argument
 * unbinds, from the notes, and rt_nulldev_bind releases the old state either way. */
COM_METHOD(ID3D11DeviceContext, void, RSSetState, ID3D11RasterizerState *pRasterizerState)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_bind(&c->rasterizer,
                    rt_nulldev_arg(ctx, pRasterizerState, &rt_nulldev_rasterizer_class, "pRasterizerState"));
    /* The seam (rt/d3dback.h, rule 2): the state crosses as its back object, NULL as NULL. */
    RT_BACK(ctx, RSSetState, c->child.back, rt_d3dback_of(pRasterizerState));
}
