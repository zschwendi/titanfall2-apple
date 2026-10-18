/* The forward of ID3D11DeviceContext::OMSetDepthStencilState through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the
 * depth-stencil state cross as their back objects, NULL as NULL; the stencil reference crosses as
 * the game passed it.  Rule 1: every stop comes before the back end is reached.  The body has no
 * RT_UNIMPLEMENTED; its stop is an object of the wrong class. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, OMSetDepthStencilState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11DepthStencilState *dss = T_BACK_NEW(device, &rt_nulldev_depthstencil_class);

    T_CASE("a depth-stencil state is bound, its stencil reference is stored, and it is handed on as its back object");
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, dss, 7);
    T_TRUE(c->depth_stencil == dss);
    T_EQ(c->stencil_ref, 7);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetDepthStencilState), 1);
        T_EQ(T_BACK_ARG(OMSetDepthStencilState, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(OMSetDepthStencilState, 1), T_BACK_OF(dss));
        T_TRUE(T_BACK_OF(dss) != T_BACK_PTR(dss));
        T_EQ(T_BACK_ARG(OMSetDepthStencilState, 2), 7);
    }

    T_CASE("a NULL depth-stencil state is handed on as NULL");
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, NULL, 0);
    T_TRUE(c->depth_stencil == NULL);
    T_EQ(c->stencil_ref, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetDepthStencilState), 2);
        T_EQ(T_BACK_ARG(OMSetDepthStencilState, 1), 0);
        T_EQ(T_BACK_ARG(OMSetDepthStencilState, 2), 0);
    }

    /* every stop of the body, before the back end */
    ID3D11DepthStencilState *query_as_dss = (ID3D11DepthStencilState *)T_BACK_NEW(device, &rt_nulldev_query_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, query_as_dss, 0));
}
