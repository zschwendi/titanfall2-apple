/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-draw
 * RT-CASES:  with no back end the call is accepted and nothing happens; with a back end it is handed on once, with its two numbers
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* A draw: the worked example of its shape for the back-end rows (unit pic-seam; rt/d3dback.h).
 * The device draws nothing (docs/handoff/d3d-contract.md, section 7: "draws nothing: the device
 * has no pixels").  The front has nothing to check in a draw's numbers: Direct3D takes any count
 * and location, and what they mean depends on what is bound, which is the renderer's to read.
 * So the body is the live-context check every method makes and the call handed on, the numbers
 * as they came. */
COM_METHOD(ID3D11DeviceContext, void, Draw, UINT VertexCount, UINT StartVertexLocation)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    RT_BACK(ctx, Draw, c->child.back, VertexCount, StartVertexLocation);
}
