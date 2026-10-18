/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-hsgetshader
 * RT-CASES:  a Get reads NULL, writes a count of 0 and leaves the class array alone; a Set of NULL, then a Get, reads NULL and a count of 0; a Get with a NULL count writes only the shader
 * RT-TRAPS:  HSGetShader with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented by
 * one", the reference rule for a returned hull shader.  None is returned: c->hs is always NULL
 * (finding 31, section 5), and rt_nulldev_ref(NULL) is NULL, which holds no reference.  The page
 * marks ppHullShader [out], not optional, so a NULL one is a stop.  No class instance is returned,
 * so the count is written as 0 and the array is not written.  The stop is the notes' rule, not the
 * page's. */
COM_METHOD(ID3D11DeviceContext, void, HSGetShader, ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!ppHullShader)
        RT_UNIMPLEMENTED(ctx, "HSGetShader with a NULL out-pointer");
    *ppHullShader = rt_nulldev_ref(c->hs);
    if (pNumClassInstances)
        *pNumClassInstances = 0;
    (void)ppClassInstances;
}
