/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshader
 * RT-CASES:  a slot never set reads NULL; a Set binds the shader and holds one reference to it; a Set over a bound shader releases the old one; a NULL shader unbinds it and releases the shader; a Set with no class instances ignores the instance array
 * RT-TRAPS:  PSSetShader with
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "The method will hold a reference to the interfaces passed in", the
 * reference the bound pixel shader holds.  NULL unbinds.  The class-instance stop and the
 * ignored ppClassInstances are the notes' rules, not the page's: the game creates no class
 * linkage (finding 31, section 4), so no class instance can exist. */
COM_METHOD(ID3D11DeviceContext, void, PSSetShader, ID3D11PixelShader *pPixelShader, ID3D11ClassInstance const **ppClassInstances, UINT NumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (NumClassInstances != 0)
        RT_UNIMPLEMENTED(ctx, "PSSetShader with %u class instances", NumClassInstances);
    (void)ppClassInstances;
    rt_nulldev_bind(&c->ps, rt_nulldev_arg(ctx, pPixelShader, &rt_nulldev_ps_class, "pPixelShader"));
    /* The seam (rt/d3dback.h, rule 2): the shader crosses as its back object, NULL as NULL.  Class
     * instances do not cross: the front has stopped on any. */
    RT_BACK(ctx, PSSetShader, c->child.back, rt_d3dback_of(pPixelShader));
}
