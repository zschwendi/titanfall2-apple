/* The forward of ID3D11DeviceContext::RSSetState through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the
 * rasterizer state cross as their back objects, NULL as NULL.  Rule 1: every stop comes before
 * the back end is reached.  The body has no RT_UNIMPLEMENTED; its one stop is an argument of
 * the wrong class (rt_nulldev_arg), so that is the stop tested here. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, RSSetState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11RasterizerState *rs = T_BACK_NEW(device, &rt_nulldev_rasterizer_class);

    T_CASE("a rasterizer state is bound, and handed on as its back object");
    COM_CALL(ID3D11DeviceContext, RSSetState, context, rs);
    T_TRUE(c->rasterizer == rs);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(RSSetState), 1);
        T_EQ(T_BACK_ARG(RSSetState, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(RSSetState, 1), T_BACK_OF(rs));
        T_TRUE(T_BACK_OF(rs) != T_BACK_PTR(rs));
    }

    T_CASE("a NULL rasterizer state is handed on as NULL");
    COM_CALL(ID3D11DeviceContext, RSSetState, context, NULL);
    T_TRUE(c->rasterizer == NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(RSSetState), 2);
        T_EQ(T_BACK_ARG(RSSetState, 1), 0);
    }

    /* every stop of the body, before the back end */
    ID3D11RasterizerState *query_as_rs = (ID3D11RasterizerState *)T_BACK_NEW(device, &rt_nulldev_query_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, RSSetState, context, query_as_rs));
}
