/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-begin
 * RT-CASES:  Begin of an occlusion query records that it began; after End it can begin again; Begin of an event or a timestamp query does nothing: the query has not begun and answers as without it
 * RT-TRAPS:  Begin of a NULL query; Begin of a query that has begun and not ended
 */
#include "rt/nulldev.h"

/* Records that the query began (NdQuery.begun); GetData.c has what the kinds answer.
 * D3D11_QUERY's page (read 2026-10-11, unit d3d-fix-3), under D3D11_QUERY_EVENT and under
 * D3D11_QUERY_TIMESTAMP: "When using this type of query, ID3D11DeviceContext::Begin is
 * disabled."  Read as: the call does nothing (Begin returns nothing, and the page names no
 * error), so the query is not marked begun and GetData answers as if Begin had not been called
 * (G9 of docs/reviews/d3d-code-review.md; the game calls Begin only on the disjoint query).
 * What Windows does with a second Begin before End, or with a NULL pointer, is not on the
 * pages: stops. */
COM_METHOD(ID3D11DeviceContext, void, Begin, ID3D11Asynchronous *pAsync)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pAsync) RT_UNIMPLEMENTED(ctx, "Begin of a NULL query");
    NdQuery *q = rt_com_state(ctx, rt_nulldev_arg(ctx, pAsync, &rt_nulldev_query_class, "pAsync"), &rt_nulldev_query_class);
    if (q->desc.Query == D3D11_QUERY_EVENT || q->desc.Query == D3D11_QUERY_TIMESTAMP) {            /* "Begin is disabled" */
        RT_BACK(ctx, Begin, c->child.back, rt_d3dback_of(pAsync));     /* the game's call, as it made it */
        return;
    }
    if (q->begun) RT_UNIMPLEMENTED(ctx, "Begin of a query that has begun and not ended (kind %u)", (unsigned)q->desc.Query);
    q->begun = 1;
    q->draws_begin = rt_nulldev_draws();
    /* The seam (rt/d3dback.h, rule 2), after the last check: the query crosses as its back object. */
    RT_BACK(ctx, Begin, c->child.back, rt_d3dback_of(pAsync));
}
