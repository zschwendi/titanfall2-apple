/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-dssetshader
 * RT-CASES:  a fresh context has no domain shader; a Set of NULL with no class instances binds nothing and releases nothing
 * RT-TRAPS:  DSSetShader with a shader; class instances
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference a bound domain shader would hold.  The page marks pDomainShader and ppClassInstances
 * [in, optional]; passing NULL disables the shader.  The two stops are the notes' rules, not the
 * page's.  No class of this device presents ID3D11DomainShader (finding 31, section 5), so a
 * non-NULL shader is a stop and c->ds stays NULL. */
COM_METHOD(ID3D11DeviceContext, void, DSSetShader, ID3D11DomainShader *pDomainShader, ID3D11ClassInstance const **ppClassInstances, UINT NumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (pDomainShader)
        RT_UNIMPLEMENTED(ctx, "DSSetShader with a shader (%p)", (void *)pDomainShader);
    if (NumClassInstances != 0)
        RT_UNIMPLEMENTED(ctx, "DSSetShader with %u class instances", NumClassInstances);
    (void)ppClassInstances;
    /* The Set binds the NULL shader: c->ds was NULL and stays NULL, releasing nothing. */
    rt_nulldev_bind(&c->ds, NULL);
    /* The seam (rt/d3dback.h, rule 2): the front has stopped on any shader and class instance, so the
     * call that reaches here is the call with NULL, and NULL crosses as NULL. */
    RT_BACK(ctx, DSSetShader, c->child.back, NULL);
}
