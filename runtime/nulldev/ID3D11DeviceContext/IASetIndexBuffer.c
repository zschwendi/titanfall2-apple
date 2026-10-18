/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetindexbuffer
 * RT-CASES:  a buffer is handed on as its back object and a NULL as NULL, with the format and the offset as passed; a buffer of another class is a stop
 * RT-TRAPS:  is an object of class
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-12: pIndexBuffer is [in, optional], so NULL unbinds; Format and Offset are
 * passed on as the game gave them.  The buffer is an argument, so the class check is a stop. */
COM_METHOD(ID3D11DeviceContext, void, IASetIndexBuffer, ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    (void)rt_nulldev_arg(ctx, pIndexBuffer, &rt_nulldev_buffer_class, "pIndexBuffer");
    RT_BACK(ctx, IASetIndexBuffer, c->child.back, rt_d3dback_of(pIndexBuffer), Format, Offset);
}
