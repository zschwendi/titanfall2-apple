/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawinstancedindirect
 * RT-CASES:  with no back end the call is accepted and nothing is recorded; with a back end it is handed on once, the buffer as its back object and the offset unchanged
 * RT-TRAPS:  DrawInstancedIndirect with a NULL buffer; pBufferForArgs (...) is an object of class ..., where ... is expected
 */
#include "rt/nulldev.h"

COM_METHOD(ID3D11DeviceContext, void, DrawInstancedIndirect, ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pBufferForArgs) RT_UNIMPLEMENTED(ctx, "DrawInstancedIndirect with a NULL buffer");
    (void)rt_nulldev_arg(ctx, pBufferForArgs, &rt_nulldev_buffer_class, "pBufferForArgs");
    RT_BACK(ctx, DrawInstancedIndirect, c->child.back, rt_d3dback_of(pBufferForArgs), AlignedByteOffsetForArgs);
}
