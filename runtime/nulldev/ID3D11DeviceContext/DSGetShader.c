/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-dsgetshader
 * RT-CASES:  a Get with no domain shader bound gives NULL and a count of 0 and leaves the class instances alone; a Get after a Set of NULL gives NULL and a count of 0; a NULL pNumClassInstances is accepted
 * RT-TRAPS:  DSGetShader with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any returned interfaces will have their reference count incremented
 * by one", the reference rule for the domain shader handed out (rt_nulldev_ref).  The page marks
 * ppDomainShader [out] and pNumClassInstances [in, out, optional]; the stop on a NULL
 * ppDomainShader and the write of 0 through pNumClassInstances are the notes' rules, not the
 * page's.  The domain-shader slot c->ds is always NULL (no class presents ID3D11DomainShader,
 * finding 31, section 5), so the Get returns NULL and ppClassInstances is not written. */
COM_METHOD(ID3D11DeviceContext, void, DSGetShader, ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!ppDomainShader)
        RT_UNIMPLEMENTED(ctx, "DSGetShader with a NULL out-pointer");
    (void)ppClassInstances;
    *ppDomainShader = rt_nulldev_ref(c->ds);
    if (pNumClassInstances)
        *pNumClassInstances = 0;
}
