/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-dispatch
 * RT-CASES:  with no back end the call is accepted and nothing happens; with a back end it is handed on once, with its three numbers
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* A compute dispatch: the worked example of its shape for the back-end rows (unit back-dispatch;
 * rt/d3dback.h).  The device runs nothing (docs/handoff/d3d-contract.md, section 7: "draws
 * nothing: the device has no pixels").  The front has nothing to check in the three group
 * counts: Direct3D takes any count, and what they mean depends on the bound shader, which is the
 * renderer's to read.  So the body is the live-context check every method makes and the call
 * handed on, the three numbers as they came. */
COM_METHOD(ID3D11DeviceContext, void, Dispatch, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    RT_BACK(ctx, Dispatch, c->child.back, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
