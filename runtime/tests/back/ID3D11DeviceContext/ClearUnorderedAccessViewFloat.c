/* The forward of ID3D11DeviceContext::ClearUnorderedAccessViewFloat through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the view
 * cross as their back objects, NULL as NULL; the values cross by pointer, unchanged.  Rule 1:
 * every stop comes before the back end is reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, ClearUnorderedAccessViewFloat)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11UnorderedAccessView *view = T_BACK_NEW(device, &rt_nulldev_uav_class);
    FLOAT v[4] = { 1.0f, 2.0f, 3.0f, 4.0f };

    T_CASE("a clear of a view to floats is handed on once, with its view and its values by pointer");
    COM_CALL(ID3D11DeviceContext, ClearUnorderedAccessViewFloat, context, view, v);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ClearUnorderedAccessViewFloat), 1);
        T_EQ(T_BACK_ARG(ClearUnorderedAccessViewFloat, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(ClearUnorderedAccessViewFloat, 1), T_BACK_OF(view));
        T_EQ(T_BACK_ARG(ClearUnorderedAccessViewFloat, 2), T_BACK_PTR(v));
    }

    /* every stop of the body, before the back end */
    ID3D11UnorderedAccessView *dsv_as_uav = (ID3D11UnorderedAccessView *)T_BACK_NEW(device, &rt_nulldev_dsv_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearUnorderedAccessViewFloat, context, NULL, v));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearUnorderedAccessViewFloat, context, view, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, ClearUnorderedAccessViewFloat, context, dsv_as_uav, v));
}
