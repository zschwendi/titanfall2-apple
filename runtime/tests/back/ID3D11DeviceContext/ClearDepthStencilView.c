/* The forward of ID3D11DeviceContext::ClearDepthStencilView through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the view
 * cross as their back objects, NULL as NULL; the flags, the depth and the stencil cross by value.
 * The recording back end keeps a FLOAT's bits, so 0.5f is 0x3f000000.  Rule 1: every stop comes
 * before the back end is reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, ClearDepthStencilView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11DepthStencilView *view = T_BACK_NEW(device, &rt_nulldev_dsv_class);

    T_CASE("a clear of depth and stencil is handed on once, with its view, flags, depth and stencil");
    COM_CALL(ID3D11DeviceContext, ClearDepthStencilView, context, view, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.5f, 7);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ClearDepthStencilView), 1);
        T_EQ(T_BACK_ARG(ClearDepthStencilView, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(ClearDepthStencilView, 1), T_BACK_OF(view));
        T_EQ(T_BACK_ARG(ClearDepthStencilView, 2), 3);
        T_EQ(T_BACK_ARG(ClearDepthStencilView, 3), 0x3f000000);
        T_EQ(T_BACK_ARG(ClearDepthStencilView, 4), 7);
    }

    /* every stop of the body, before the back end */
    ID3D11DepthStencilView *query_as_dsv = (ID3D11DepthStencilView *)T_BACK_NEW(device, &rt_nulldev_query_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearDepthStencilView, context, NULL, D3D11_CLEAR_DEPTH, 0.5f, 7));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearDepthStencilView, context, view, 4, 0.5f, 7));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearDepthStencilView, context, query_as_dsv, D3D11_CLEAR_DEPTH, 0.5f, 7));
}
