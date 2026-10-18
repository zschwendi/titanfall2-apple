/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-gssetshader
 * RT-CASES:  a slot never set reads NULL; a Set binds the shader and holds one reference to it; a Set over an occupied slot releases the old shader; a NULL shader unbinds the slot and releases the shader; a count of 0 with a class-instance array binds the shader
 * RT-TRAPS:  GSSetShader with
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference the bound shader holds.  The page marks pShader [in, optional] and says that NULL disables
 * the shader.  It prints ppClassInstances as ID3D11ClassInstance * const *, where the COM_METHOD line
 * here has ID3D11ClassInstance const **; the COM_METHOD line is the one the build compares.  The page
 * gives the maximum number of instances a shader can have as 256 and does not say what a count of
 * 0 means beyond that; the stop for a count other than 0 is the notes' rule: the game never creates a
 * class linkage, so no instance can exist. */
COM_METHOD(ID3D11DeviceContext, void, GSSetShader, ID3D11GeometryShader *pShader, ID3D11ClassInstance const **ppClassInstances, UINT NumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    (void)ppClassInstances;
    if (NumClassInstances != 0)
        RT_UNIMPLEMENTED(ctx, "GSSetShader with %u class instances", NumClassInstances);
    rt_nulldev_bind(&c->gs, rt_nulldev_arg(ctx, pShader, &rt_nulldev_gs_class, "pShader"));
    /* The seam (rt/d3dback.h, rule 2): the shader crosses as its back object, NULL as NULL.  Class
     * instances do not cross: the front has stopped on any. */
    RT_BACK(ctx, GSSetShader, c->child.back, rt_d3dback_of(pShader));
}
