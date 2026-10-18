/* The forward of ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews through the seam
 * (rt/d3dback.h).  Expected values: the seam's rules and the method's notes, not the body.
 * Rule 2: each view crosses as its back object, NULL as NULL, in arrays rebuilt on the stack; the
 * depth view with the render targets, and the render-target and UAV arrays, cross as NULL under
 * the keep value 0xffffffff; the counts and UAVStartSlot cross as passed; the initial counts
 * cross by pointer.  Rule 1: each of the body's stops comes before the back end is reached.
 * The front's own record (NdContext.rtv, dsv) is checked outside `if (T_BACK)`, the same with and
 * without a back end. */
#include "rt/test_back.h"

/* The view's resource, as the nulldev test sets it (the view holds the texture). */
static void *attach(void *view, const RtComClass *cls, void *texture)
{
    NdView *v = rt_com_state_of(view, cls);
    rt_com_addref(texture);
    v->resource = (ID3D11Resource *)texture;
    return view;
}

BACK_TEST(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11RenderTargetView *rtv = T_BACK_NEW(device, &rt_nulldev_rtv_class);
    ID3D11DepthStencilView *dsv = T_BACK_NEW(device, &rt_nulldev_dsv_class);
    ID3D11UnorderedAccessView *uav = T_BACK_NEW(device, &rt_nulldev_uav_class);
    ID3D11RenderTargetView const *rtvs[1] = { rtv };
    ID3D11UnorderedAccessView const *uavs[1] = { uav };
    ID3D11RenderTargetView const *nine[9] = { rtv, rtv, rtv, rtv, rtv, rtv, rtv, rtv, rtv };
    ID3D11RenderTargetView const *wrong[1] = { (ID3D11RenderTargetView const *)T_BACK_NEW(device, &rt_nulldev_query_class) };
    ID3D11UnorderedAccessView const *wrong_uav[1] = { (ID3D11UnorderedAccessView const *)T_BACK_NEW(device, &rt_nulldev_query_class) };
    UINT counts[1] = { 0 };

    T_CASE("one render target in slot 0, the depth view, one UAV from slot 1: each view crosses as its back object");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, rtvs, dsv, 1, 1, uavs, NULL);
    T_TRUE(c->rtv[0] == rtv && c->dsv == dsv);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetRenderTargetsAndUnorderedAccessViews), 1);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 1), 1);
        T_EQ(T_BACK_ELEMENT(OMSetRenderTargetsAndUnorderedAccessViews, 2, 0), T_BACK_OF(rtv));
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 3), T_BACK_OF(dsv));
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 4), 1);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 5), 1);
        T_EQ(T_BACK_ELEMENT(OMSetRenderTargetsAndUnorderedAccessViews, 6, 0), T_BACK_OF(uav));
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 7), 0);
    }

    T_CASE("keep value 0xffffffff for the render targets and UAVs: both arrays cross as NULL, the depth view too, counts as passed");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 0xffffffff, NULL, NULL, 1, 1, uavs, counts);
    T_TRUE(c->rtv[0] == rtv && c->dsv == dsv);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetRenderTargetsAndUnorderedAccessViews), 2);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 1), 0xffffffff);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 2), 0);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 3), 0);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 4), 1);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 5), 1);
        T_EQ(T_BACK_ARG(OMSetRenderTargetsAndUnorderedAccessViews, 7), T_BACK_PTR(counts));
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, rtvs, dsv, 7, 2, uavs, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 2, nine, dsv, 1, 1, uavs, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, NULL, NULL, 0, 0, NULL, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 9, nine, NULL, 0, 0, NULL, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, NULL, NULL, 0, 0, NULL, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, rtvs, dsv, 1, 1, NULL, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, rtvs, dsv, 1, 1, wrong_uav, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, wrong, dsv, 1, 1, uavs, NULL));

    /* a render target bound from a texture, and a UAV of the same texture from slot 1, kept: stop 7 */
    void *tex = T_BACK_ADOPT(rt_nulldev_new(device, &rt_nulldev_texture2d_class));
    ID3D11RenderTargetView *rtT = attach(T_BACK_NEW(device, &rt_nulldev_rtv_class), &rt_nulldev_rtv_class, tex);
    ID3D11UnorderedAccessView *uavT = attach(T_BACK_NEW(device, &rt_nulldev_uav_class), &rt_nulldev_uav_class, tex);
    ID3D11RenderTargetView const *rtsT[1] = { rtT };
    ID3D11UnorderedAccessView const *uavsT[1] = { uavT };
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, 1, rtsT, dsv, 1, 0, NULL, NULL);
    T_TRUE(c->rtv[0] == rtT);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargetsAndUnorderedAccessViews, context, D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, NULL, NULL, 1, 1, uavsT, NULL));
}
