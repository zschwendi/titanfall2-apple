/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-gsgetshader
 * RT-CASES:  a slot never set reads NULL; a Get hands out the bound shader with one more reference; a Get writes 0 through the count and leaves the class-instance array alone; a NULL count pointer is allowed; a Get after a Set of another shader hands out that shader; a Get after a Set of NULL hands out NULL
 * RT-TRAPS:  GSGetShader with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented by
 * one", the reference the caller holds on the shader it is handed.  The page marks ppGeometryShader
 * [out] (not optional), so a NULL out-pointer is the notes' stop.  The page marks pNumClassInstances
 * [in, out, optional] and ppClassInstances [out, optional]; writing 0 through the count and never
 * writing the array are the notes' rules: no class instance is ever bound. */
COM_METHOD(ID3D11DeviceContext, void, GSGetShader, ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    (void)ppClassInstances;
    if (!ppGeometryShader)
        RT_UNIMPLEMENTED(ctx, "GSGetShader with a NULL out-pointer");
    *ppGeometryShader = rt_nulldev_ref(c->gs);
    if (pNumClassInstances)
        *pNumClassInstances = 0;
}
