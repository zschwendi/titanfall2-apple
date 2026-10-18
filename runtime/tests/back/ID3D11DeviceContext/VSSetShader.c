/* The forward of ID3D11DeviceContext::VSSetShader through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the shader
 * cross as their back objects, NULL as NULL.  Rule 1: every stop comes before the back end is
 * reached.  A class instance does not cross: the front stops on any, so the call with one is a
 * stop and not a forward. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, VSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11VertexShader *vs = T_BACK_NEW(device, &rt_nulldev_vs_class);

    T_CASE("a shader is bound, and handed on as its back object");
    COM_CALL(ID3D11DeviceContext, VSSetShader, context, vs, NULL, 0);
    T_TRUE(c->vs == vs);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(VSSetShader), 1);
        T_EQ(T_BACK_ARG(VSSetShader, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(VSSetShader, 1), T_BACK_OF(vs));
        T_TRUE(T_BACK_OF(vs) != T_BACK_PTR(vs));
    }

    T_CASE("a NULL shader is handed on as NULL");
    COM_CALL(ID3D11DeviceContext, VSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->vs == NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(VSSetShader), 2);
        T_EQ(T_BACK_ARG(VSSetShader, 1), 0);
    }

    /* every stop of the body, before the back end */
    ID3D11VertexShader *ps_as_vs = (ID3D11VertexShader *)T_BACK_NEW(device, &rt_nulldev_ps_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, VSSetShader, context, vs, NULL, 1));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, VSSetShader, context, ps_as_vs, NULL, 0));
}
