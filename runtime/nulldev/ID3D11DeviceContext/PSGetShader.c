/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-psgetshader
 * RT-CASES:  a Get of an unset shader returns NULL and writes 0 instances; a Get returns the bound shader with one more reference; a NULL instance count is allowed and writes nothing
 * RT-TRAPS:  PSGetShader with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one", the reference handed out for the bound pixel shader.  The 0 written through
 * pNumClassInstances and the untouched ppClassInstances are the notes' rules: no class instance
 * is ever bound.  The NULL out-pointer stop is the notes' rule. */
COM_METHOD(ID3D11DeviceContext, void, PSGetShader, ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!ppPixelShader)
        RT_UNIMPLEMENTED(ctx, "PSGetShader with a NULL out-pointer");
    (void)ppClassInstances;
    *ppPixelShader = rt_nulldev_ref(c->ps);
    if (pNumClassInstances)
        *pNumClassInstances = 0;
}
