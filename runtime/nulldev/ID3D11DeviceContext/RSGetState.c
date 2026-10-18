/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rsgetstate
 * RT-CASES:  a Get returns the bound rasterizer state with one more reference; a Get with nothing bound returns NULL
 * RT-TRAPS:  RSGetState with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one", the reference the returned rasterizer state gets for the caller.  The NULL out-pointer
 * stop is the notes' rule, not the page's. */
COM_METHOD(ID3D11DeviceContext, void, RSGetState, ID3D11RasterizerState **ppRasterizerState)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!ppRasterizerState)
        RT_UNIMPLEMENTED(ctx, "RSGetState with a NULL out-pointer");
    *ppRasterizerState = rt_nulldev_ref(c->rasterizer);
}
