/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cssetshader
 * RT-CASES:  a Set with no class instances is handed on, with the shader as its back object; a NULL shader is handed on as NULL; a Set with class instances stops; an object of another class stops
 * RT-TRAPS:  class instances; is an object of class
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-12: the compute shader is handed on as the back object (rule 2) after the
 * checks.  The game never creates a class linkage (finding 31, section 4), so any NumClassInstances
 * other than 0 is a stop (the same as VSSetShader). */
COM_METHOD(ID3D11DeviceContext, void, CSSetShader, ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance const **ppClassInstances, UINT NumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (NumClassInstances != 0)
        RT_UNIMPLEMENTED(ctx, "CSSetShader with %u class instances", NumClassInstances);
    (void)ppClassInstances;
    (void)rt_nulldev_arg(ctx, pComputeShader, &rt_nulldev_cs_class, "pComputeShader");
    /* The seam (rt/d3dback.h, rule 2): the shader crosses as its back object, NULL as NULL. */
    RT_BACK(ctx, CSSetShader, c->child.back, rt_d3dback_of(pComputeShader));
}
