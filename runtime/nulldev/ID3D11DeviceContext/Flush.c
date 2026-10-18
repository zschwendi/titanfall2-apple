/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush
 * RT-CASES:  the call is accepted and handed on to the back end once per call, with the context's back object
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* The device draws nothing and records nothing of its own; Flush has no arguments and no stop.
 * The body checks that `this` is a live context, then hands the call on (rt/d3dback.h). */
COM_METHOD(ID3D11DeviceContext, void, Flush)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    RT_BACK(ctx, Flush, c->child.back);
}
