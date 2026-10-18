/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vsgetshader
 * RT-CASES:  a Get hands out the bound shader with one more reference; a Get of a shader never set returns NULL; a Get writes 0 to a non-NULL pNumClassInstances; a Get leaves ppClassInstances unwritten
 * RT-TRAPS:  VSGetShader with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented by
 * one", the reference handed to the caller.  The page's class-instance array is not written here:
 * no instance is ever bound, so the count written is 0 (the notes' rule).  A NULL ppVertexShader
 * is a stop, the notes' rule. */
COM_METHOD(ID3D11DeviceContext, void, VSGetShader, ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!ppVertexShader)
        RT_UNIMPLEMENTED(ctx, "VSGetShader with a NULL out-pointer");
    *ppVertexShader = rt_nulldev_ref(c->vs);
    if (pNumClassInstances)
        *pNumClassInstances = 0;
    (void)ppClassInstances;
}
