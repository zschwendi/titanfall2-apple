/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearstate
 * RT-CASES:  a bind before the call holds one reference to each object; ClearState nulls every pointer of the record; ClearState releases each object it unbinds; topology, viewport count and stencil reference go to 0; the first viewport is all zero bytes; sample mask goes to 0xffffffff and the blend factor to four times 1.0f; child.device is unchanged
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Restore all default settings. This method resets any device context to the
 * default settings. This sets all input/output resource slots, shaders, input layouts, predications,
 * scissor rectangles, depth-stencil state, rasterizer state, blend state, sampler state, and viewports
 * to NULL. The primitive topology is set to UNDEFINED."  The page does not name the blend factor, the
 * sample mask or the stencil reference, and the record holds no predication or scissor state; the
 * values NdContext gives them after a reset are the notes' rules, and rt_nulldev_context_reset does
 * the whole of the work.  The back end is handed the call after the reset: rule 1 (no check to pass
 * here), and rt_nulldev_context_reset keeps the context's back object (it is not state). */
COM_METHOD(ID3D11DeviceContext, void, ClearState)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    rt_nulldev_context_reset(c);
    RT_BACK(ctx, ClearState, c->child.back);
}
