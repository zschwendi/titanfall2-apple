/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawindexed
 * RT-CASES:  with no back end the call is accepted and nothing happens; with a back end it is handed on once, with its three numbers
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* A draw: the worked example of its shape for the back-end rows (unit pic-seam; rt/d3dback.h).
 * The device draws nothing (docs/handoff/d3d-contract.md, section 7: "draws nothing: the device
 * has no pixels"); until this unit the method was a line of runtime/com/accept.def.  The front
 * has nothing to check in a draw's numbers: Direct3D takes any count and location, and what
 * they mean depends on what is bound, which is the renderer's to read.  So the body is the
 * live-context check every method makes and the call handed on, the numbers as they came
 * (BaseVertexLocation is signed). */
COM_METHOD(ID3D11DeviceContext, void, DrawIndexed, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    RT_BACK(ctx, DrawIndexed, c->child.back, IndexCount, StartIndexLocation, BaseVertexLocation);
}
