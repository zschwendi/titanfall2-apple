/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetviewports
 * RT-CASES:  a Set stores the viewports and their count; a Set with fewer viewports replaces the count; a Set with 0 viewports disables all; the largest count of 16 is accepted
 * RT-TRAPS:  RSSetViewports with; RSSetViewports with a NULL array
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Any viewports not defined by
 * the call are disabled", so the count becomes NumViewports.  The count's limit ND_VIEWPORTS is 16,
 * from the notes.  The NULL-array stop is the notes' rule. */
COM_METHOD(ID3D11DeviceContext, void, RSSetViewports, UINT NumViewports, const D3D11_VIEWPORT *pViewports)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (NumViewports > ND_VIEWPORTS)
        RT_UNIMPLEMENTED(ctx, "RSSetViewports with %u viewports", NumViewports);
    if (NumViewports > 0 && !pViewports)
        RT_UNIMPLEMENTED(ctx, "RSSetViewports with a NULL array");
    if (NumViewports > 0)
        memcpy(c->viewport, pViewports, (size_t)NumViewports * sizeof(D3D11_VIEWPORT));
    c->nviewports = NumViewports;
    /* The seam (rt/d3dback.h, rule 2): the count is a value and the array crosses by pointer. */
    RT_BACK(ctx, RSSetViewports, c->child.back, NumViewports, pViewports);
}
