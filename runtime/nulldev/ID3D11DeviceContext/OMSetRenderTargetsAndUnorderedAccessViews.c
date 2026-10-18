/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargetsandunorderedaccessviews
 * RT-CASES:  two views and a depth view bound with UAVStartSlot 2 and no UAVs; keep value with one UAV leaves render targets and depth-stencil view bound; with the UAV keep value one view is recorded and slot 1 released, whatever UAVStartSlot is; a shader-resource view of a UAV's texture is unbound; a shader-resource view of a render-target's texture is unbound while a view of another texture stays
 * RT-TRAPS:  views; OMSetRenderTargetsAndUnorderedAccessViews with unordered-access views in slots; OMSetRenderTargetsAndUnorderedAccessViews with UAVs from slot; OMSetRenderTargetsAndUnorderedAccessViews with a NULL array; OMSetRenderTargetsAndUnorderedAccessViews with a NULL array of unordered-access views; OMSetRenderTargetsAndUnorderedAccessViews keeping render targets with one bound in slot; OMSetRenderTargetsAndUnorderedAccessViews with an unordered-access view of a kept render target
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: the page's remarks give the conditions for the stops (NumRTVs <= 8,
 * UAVStartSlot >= NumRTVs, UAVStartSlot + NumUAVs <= 8), the keep values, and that a keep value
 * for NumRTVs ignores ppDepthStencilView; the set methods hold a reference to the interfaces
 * passed in.  The page says the keep value does not modify the bound RTVs AND also unbinds "All
 * RTVs in slots >= UAVStartSlot": the two sentences differ, so the body stops in that case
 * (stop 5 below) rather than choosing.  The page also says the keep value unbinds the RTVs that
 * conflict with the UAVs: the notes' stop 6 is the same disagreement.  The UAVs are not recorded
 * (no Get of them is in the game). */
COM_METHOD(ID3D11DeviceContext, void, OMSetRenderTargetsAndUnorderedAccessViews, UINT NumRTVs, ID3D11RenderTargetView const **ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView const **ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    bool keep_rt = NumRTVs == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL;
    bool keep_uav = NumUAVs == D3D11_KEEP_UNORDERED_ACCESS_VIEWS;
    if (!keep_rt && NumRTVs > ND_RT_SLOTS)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews with %u views", NumRTVs);
    if (!keep_uav && (uint64_t)UAVStartSlot + NumUAVs > D3D11_PS_CS_UAV_REGISTER_COUNT)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews with unordered-access views in slots %u to %u",
                         UAVStartSlot, UAVStartSlot + NumUAVs - 1);
    if (!keep_rt && !keep_uav && UAVStartSlot < NumRTVs)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews with UAVs from slot %u and %u render targets",
                         UAVStartSlot, NumRTVs);
    if (!keep_rt && NumRTVs > 0 && !ppRenderTargetViews)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews with a NULL array");
    if (!keep_uav && NumUAVs > 0 && !ppUnorderedAccessViews)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews with a NULL array of unordered-access views");
    if (keep_rt)
        for (UINT i = UAVStartSlot; i < ND_RT_SLOTS; i++)
            if (c->rtv[i])
                RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews keeping render targets with one bound in slot %u", i);

    if (!keep_rt) {
        for (UINT i = 0; i < ND_RT_SLOTS; i++) {
            void *view = i < NumRTVs ? rt_nulldev_arg(ctx, (void *)ppRenderTargetViews[i], &rt_nulldev_rtv_class, "ppRenderTargetViews") : NULL;
            rt_nulldev_bind(&c->rtv[i], view);
        }
        rt_nulldev_bind(&c->dsv, rt_nulldev_arg(ctx, (void *)pDepthStencilView, &rt_nulldev_dsv_class, "pDepthStencilView"));
        for (UINT i = 0; i < NumRTVs; i++)
            rt_nulldev_unbind_readers(ctx, c, c->rtv[i]);
        rt_nulldev_unbind_readers(ctx, c, c->dsv);
    }

    if (!keep_uav && ppUnorderedAccessViews) {
        for (UINT i = 0; i < NumUAVs; i++) {
            void *u = rt_nulldev_arg(ctx, (void *)ppUnorderedAccessViews[i], &rt_nulldev_uav_class, "ppUnorderedAccessViews");
            if (keep_rt && rt_nulldev_is_target(ctx, c, u))
                RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews with an unordered-access view of a kept render target");
            rt_nulldev_unbind_readers(ctx, c, u);
        }
    }

    /* The seam (rt/d3dback.h, rule 2), after every check and stop above: each view crosses as its
     * back object, NULL as NULL, in arrays rebuilt on the stack; a keep value for the render
     * targets (and the depth view with them) and one for the unordered-access views crosses as
     * NULL, and the two counts and the initial counts cross as passed. */
    if (rt_d3dback()) {
        void *rtvs[ND_RT_SLOTS], *uavs[D3D11_PS_CS_UAV_REGISTER_COUNT];
        for (UINT i = 0; !keep_rt && i < NumRTVs; i++) rtvs[i] = rt_d3dback_of((void *)ppRenderTargetViews[i]);
        for (UINT i = 0; !keep_uav && i < NumUAVs; i++) uavs[i] = rt_d3dback_of((void *)ppUnorderedAccessViews[i]);
        RT_BACK(ctx, OMSetRenderTargetsAndUnorderedAccessViews, c->child.back, NumRTVs,
                !keep_rt && ppRenderTargetViews ? rtvs : NULL, keep_rt ? NULL : rt_d3dback_of(pDepthStencilView),
                UAVStartSlot, NumUAVs, !keep_uav && ppUnorderedAccessViews ? uavs : NULL, pUAVInitialCounts);
    }
}
