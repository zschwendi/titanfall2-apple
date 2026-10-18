/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-end
 * RT-CASES:  End of an event query counts it and stamps the time; End of a begun occlusion query ends it
 * RT-TRAPS:  End of a NULL query; End of an occlusion or timestamp-disjoint query that was not begun
 */
#include "rt/nulldev.h"

/* Records that the query ended (NdQuery.ended counts, NdQuery.begun is cleared) and when
 * (NdQuery.end_ticks: what a timestamp query answers).  GetData's page (read 2026-10-10 by a
 * subagent, which paraphrased it): some queries need only End, and their data is accurate up
 * to the last End.  An End of an occlusion or timestamp-disjoint query with no Begin before it
 * is not on the pages: a stop. */
COM_METHOD(ID3D11DeviceContext, void, End, ID3D11Asynchronous *pAsync)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pAsync) RT_UNIMPLEMENTED(ctx, "End of a NULL query");
    NdQuery *q = rt_com_state(ctx, rt_nulldev_arg(ctx, pAsync, &rt_nulldev_query_class, "pAsync"), &rt_nulldev_query_class);
    if ((q->desc.Query == D3D11_QUERY_OCCLUSION || q->desc.Query == D3D11_QUERY_TIMESTAMP_DISJOINT) && !q->begun)
        RT_UNIMPLEMENTED(ctx, "End of an occlusion or timestamp-disjoint query that was not begun (kind %u)", (unsigned)q->desc.Query);
    /* An occlusion query's answer is fixed here (rt/nulldev.h, RT_D3D_OCCLUSION).  Microsoft,
     * D3D11_QUERY (as docs/reviews/null-device-redteam.md, R2, reads it): the count is "the number
     * of samples that passed the depth and stencil tests" between Begin and End.  The device
     * draws nothing, so the true count is 0, and that is the default.  With the switch set to
     * "all" it is what a draw that nothing occludes gives at most: the first viewport's area
     * times the first render target's sample count, when a draw call came since Begin. */
    q->samples = 0;
    if (q->desc.Query == D3D11_QUERY_OCCLUSION && rt_nulldev_occlusion_all() && rt_nulldev_draws() != q->draws_begin) {
        UINT count = 1;
        const NdView *v = c->rtv[0] ? rt_com_state_of(c->rtv[0], &rt_nulldev_rtv_class) : NULL;
        if (v && v->resource && rt_com_class_of(v->resource) == &rt_nulldev_texture2d_class)
            count = ((const NdTexture *)rt_com_state_of(v->resource, &rt_nulldev_texture2d_class))->desc.d2.SampleDesc.Count;
        if (c->nviewports) q->samples = (uint64_t)(c->viewport[0].Width * c->viewport[0].Height) * count;
    }
    q->begun = 0;
    q->ended++;
    q->end_ticks = rt_nulldev_ticks();
    /* The seam (rt/d3dback.h, rule 2): the query crosses as its back object. */
    RT_BACK(ctx, End, c->child.back, rt_d3dback_of(pAsync));
}
