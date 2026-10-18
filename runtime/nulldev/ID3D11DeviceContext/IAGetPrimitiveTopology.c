/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iagetprimitivetopology
 * RT-CASES:  a Get before any Set reads the undefined topology 0; a Get returns the topology last set
 * RT-TRAPS:  IAGetPrimitiveTopology with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* The default topology, undefined (0), is from the notes (Microsoft's page of ClearState, from
 * memory).  The NULL out-pointer stop is the notes' rule, not the page's. */
COM_METHOD(ID3D11DeviceContext, void, IAGetPrimitiveTopology, D3D11_PRIMITIVE_TOPOLOGY *pTopology)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pTopology)
        RT_UNIMPLEMENTED(ctx, "IAGetPrimitiveTopology with a NULL out-pointer");
    *pTopology = c->topology;
}
