/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rsgetviewports
 * RT-CASES:  a Get with a NULL array returns the count; a Get with room fills the viewports, zeroes the rest and returns the count; a Get with room for exactly the count returns them all; a Get on a new context returns 0
 * RT-TRAPS:  RSGetViewports with a NULL count; RSGetViewports for; RSGetViewports with room for
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: pNumViewports is in and out;
 * with a NULL array only the count is returned; the unused elements of the array contain 0, and
 * the count returned is the number of viewports set.  The limit ND_VIEWPORTS is 16, from the notes.
 * The stops are the notes' rules: the page does not say what the count is when the room is short. */
COM_METHOD(ID3D11DeviceContext, void, RSGetViewports, UINT *pNumViewports, D3D11_VIEWPORT *pViewports)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pNumViewports)
        RT_UNIMPLEMENTED(ctx, "RSGetViewports with a NULL count");
    if (!pViewports) {
        *pNumViewports = c->nviewports;
        return;
    }
    UINT n = *pNumViewports;
    if (n > ND_VIEWPORTS)
        RT_UNIMPLEMENTED(ctx, "RSGetViewports for %u viewports", n);
    if (n < c->nviewports)
        RT_UNIMPLEMENTED(ctx, "RSGetViewports with room for %u of %u viewports", n, c->nviewports);
    memcpy(pViewports, c->viewport, (size_t)c->nviewports * sizeof(D3D11_VIEWPORT));
    memset(pViewports + c->nviewports, 0, (size_t)(n - c->nviewports) * sizeof(D3D11_VIEWPORT));
    *pNumViewports = c->nviewports;
}
