/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetshader
 * RT-CASES:  a shader never set reads NULL; a Set binds the shader and holds one reference to it; a Set over a bound shader releases the old one; a NULL shader unbinds and releases; a Set with no class instances and a NULL array is accepted
 * RT-TRAPS:  class instances
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference the bound shader holds.  The page says ppClassInstances is NULL when the shader uses no
 * interfaces, and that each interface used needs a class instance; the game never creates a class
 * linkage (finding 31, section 4), so any NumClassInstances other than 0 is a stop.  The NULL
 * shader unbinding is the notes' rule. */
COM_METHOD(ID3D11DeviceContext, void, VSSetShader, ID3D11VertexShader *pVertexShader, ID3D11ClassInstance const **ppClassInstances, UINT NumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (NumClassInstances != 0)
        RT_UNIMPLEMENTED(ctx, "VSSetShader with %u class instances", NumClassInstances);
    (void)ppClassInstances;
    rt_nulldev_bind(&c->vs, rt_nulldev_arg(ctx, pVertexShader, &rt_nulldev_vs_class, "pVertexShader"));
    /* The seam (rt/d3dback.h, rule 2): the shader crosses as its back object, NULL as NULL.  Class
     * instances do not cross: the front has stopped on any. */
    RT_BACK(ctx, VSSetShader, c->child.back, rt_d3dback_of(pVertexShader));
}
