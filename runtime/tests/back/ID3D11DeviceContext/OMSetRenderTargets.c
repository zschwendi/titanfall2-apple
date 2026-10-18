/* The forward of ID3D11DeviceContext::OMSetRenderTargets through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context, each view and the depth
 * view cross as their back objects, NULL as NULL, in an array rebuilt on the stack (never the
 * game's own), a NULL array as NULL; NumViews as passed, and only NumViews views (not the eight
 * slots).  Rule 1: each of the body's stops comes before the back end is reached.  What the front
 * records (NdContext.rtv, NdContext.dsv, the holds) is the same with and without a back end:
 * runtime/tests/nulldev/ID3D11DeviceContext/OMSetRenderTargets.c has those values and their sources. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, OMSetRenderTargets)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11RenderTargetView *a = T_BACK_NEW(device, &rt_nulldev_rtv_class);
    ID3D11RenderTargetView *b = T_BACK_NEW(device, &rt_nulldev_rtv_class);
    ID3D11DepthStencilView *d = T_BACK_NEW(device, &rt_nulldev_dsv_class);
    ID3D11SamplerState *sampler = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11RenderTargetView const *two[2] = { a, b };
    ID3D11RenderTargetView const *nine[9] = { a, b, a, b, a, b, a, b, a };

    T_CASE("two views and the depth view: each crosses as its back object, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 2, two, d);
    T_TRUE(c->rtv[0] == a && c->rtv[1] == b && c->dsv == d);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(d), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetRenderTargets), 1);
        T_EQ(T_BACK_ARG(OMSetRenderTargets, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(OMSetRenderTargets, 1), 2);
        T_TRUE(T_BACK_ARG(OMSetRenderTargets, 2) != 0 && T_BACK_ARG(OMSetRenderTargets, 2) != T_BACK_PTR(two));
        T_EQ(T_BACK_ARRAY_LEN(OMSetRenderTargets, 2), 2);
        T_EQ(T_BACK_ELEMENT(OMSetRenderTargets, 2, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(OMSetRenderTargets, 2, 1), T_BACK_OF(b));
        T_EQ(T_BACK_ARG(OMSetRenderTargets, 3), T_BACK_OF(d));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("no view and no depth view: both arrays and the depth view cross as NULL");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 0, NULL, NULL);
    T_TRUE(c->rtv[0] == NULL && c->rtv[1] == NULL && c->dsv == NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(d), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetRenderTargets), 2);
        T_EQ(T_BACK_ARG(OMSetRenderTargets, 1), 0);
        T_EQ(T_BACK_ARG(OMSetRenderTargets, 2), 0);
        T_EQ(T_BACK_ARG(OMSetRenderTargets, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(OMSetRenderTargets, 2), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11RenderTargetView const *wrong[1] = { (ID3D11RenderTargetView const *)sampler };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 9, nine, d));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 1, NULL, d));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 1, wrong, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 0, NULL, (ID3D11DepthStencilView *)sampler));
}
