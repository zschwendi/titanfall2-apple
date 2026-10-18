/* The forward of ID3D11DeviceContext::CSSetShader through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the shader
 * cross as their back objects, NULL as NULL.  Rule 1: every stop comes before the back end is
 * reached.  A class instance does not cross: the front stops on any, so the call with one is a
 * stop and not a forward. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, CSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11ComputeShader *cs = T_BACK_NEW(device, &rt_nulldev_cs_class);

    T_CASE("a compute shader is handed on as its back object");
    COM_CALL(ID3D11DeviceContext, CSSetShader, context, cs, NULL, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CSSetShader), 1);
        T_EQ(T_BACK_ARG(CSSetShader, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(CSSetShader, 1), T_BACK_OF(cs));
        T_TRUE(T_BACK_OF(cs) != T_BACK_PTR(cs));
    }

    T_CASE("a NULL shader is handed on as NULL");
    COM_CALL(ID3D11DeviceContext, CSSetShader, context, NULL, NULL, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CSSetShader), 2);
        T_EQ(T_BACK_ARG(CSSetShader, 1), 0);
    }

    /* every stop of the body, before the back end */
    ID3D11ComputeShader *ps_as_cs = (ID3D11ComputeShader *)T_BACK_NEW(device, &rt_nulldev_query_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CSSetShader, context, cs, NULL, 1));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, CSSetShader, context, ps_as_cs, NULL, 0));
}
