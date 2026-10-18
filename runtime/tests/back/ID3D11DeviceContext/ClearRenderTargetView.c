/* The forward of ID3D11DeviceContext::ClearRenderTargetView through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the view
 * cross as their back objects, NULL as NULL; the colour crosses by pointer, unchanged.  Rule 1:
 * every stop comes before the back end is reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, ClearRenderTargetView)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11RenderTargetView *view = T_BACK_NEW(device, &rt_nulldev_rtv_class);
    FLOAT colour[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

    T_CASE("a clear is handed on once, with its view and its colour by pointer");
    COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, view, colour);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ClearRenderTargetView), 1);
        T_EQ(T_BACK_ARG(ClearRenderTargetView, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(ClearRenderTargetView, 1), T_BACK_OF(view));
        T_EQ(T_BACK_ARG(ClearRenderTargetView, 2), T_BACK_PTR(colour));
    }

    T_CASE("every clear is handed on");
    COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, view, colour);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ClearRenderTargetView), 2);
    }

    /* every stop of the body, before the back end */
    ID3D11RenderTargetView *dsv_as_rtv = (ID3D11RenderTargetView *)T_BACK_NEW(device, &rt_nulldev_dsv_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, NULL, colour));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, view, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, dsv_as_rtv, colour));
}
