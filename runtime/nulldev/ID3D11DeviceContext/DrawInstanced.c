/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawinstanced
 * RT-CASES:  with no back end the call is accepted and nothing happens; with a back end it is handed on once, with its four numbers
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* A draw, instanced: the same shape as Draw (unit back-draw; rt/d3dback.h).  The front has
 * nothing to check in the numbers: Direct3D takes any count and location, and what they mean
 * depends on what is bound, which is the renderer's to read.  So the body is the live-context
 * check every method makes and the call handed on, the numbers as they came. */
COM_METHOD(ID3D11DeviceContext, void, DrawInstanced, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    RT_BACK(ctx, DrawInstanced, c->child.back, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}
