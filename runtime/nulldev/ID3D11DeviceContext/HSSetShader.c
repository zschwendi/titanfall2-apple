/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-hssetshader
 * RT-CASES:  the hull shader reads NULL after creation; a Set of NULL with no class instances leaves it NULL; a Set of NULL with a class array and a count of 0 does nothing
 * RT-TRAPS:  HSSetShader with a shader; class instances
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference a bound hull shader would hold.  The page marks pHullShader and ppClassInstances
 * [in, optional]; "Passing in NULL disables the shader for this pipeline stage"; "Set
 * ppClassInstances to NULL if the shader does not use any interfaces".  No class of this device
 * presents ID3D11HullShader (finding 31, section 5), so a non-NULL pHullShader is a stop and c->hs
 * is always NULL.  The NumClassInstances stop is the notes' rule, not the page's. */
COM_METHOD(ID3D11DeviceContext, void, HSSetShader, ID3D11HullShader *pHullShader, ID3D11ClassInstance const **ppClassInstances, UINT NumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (pHullShader)
        RT_UNIMPLEMENTED(ctx, "HSSetShader with a shader (%p)", (void *)pHullShader);
    if (NumClassInstances != 0)
        RT_UNIMPLEMENTED(ctx, "HSSetShader with %u class instances", NumClassInstances);
    /* Nothing is bound: c->hs is NULL already and stays NULL, so no reference is held or released. */
    (void)ppClassInstances;
    /* The seam (rt/d3dback.h, rule 2): the front has stopped on any shader and class instance, so the
     * call that reaches here is the call with NULL, and NULL crosses as NULL. */
    RT_BACK(ctx, HSSetShader, c->child.back, NULL);
}
