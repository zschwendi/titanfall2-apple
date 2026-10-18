/* The forward of ID3D11DeviceContext::DSSetShader through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context crosses as its
 * back object and NULL as NULL.  Rule 1: every stop comes before the back end is reached.  The
 * front has no domain shader, so the only call that returns is the call with NULL; a shader that is
 * not NULL and a class instance are stops and not forwards. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, DSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);

    T_CASE("a NULL domain shader with no class instances is handed on as NULL");
    COM_CALL(ID3D11DeviceContext, DSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->ds == NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DSSetShader), 1);
        T_EQ(T_BACK_ARG(DSSetShader, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(DSSetShader, 1), 0);
    }

    T_CASE("a second NULL call is handed on again");
    COM_CALL(ID3D11DeviceContext, DSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->ds == NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DSSetShader), 2);
        T_EQ(T_BACK_ARG(DSSetShader, 1), 0);
    }

    /* every stop of the body, before the back end */
    ID3D11DomainShader *shader = (ID3D11DomainShader *)T_BACK_NEW(device, &rt_nulldev_ps_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, DSSetShader, context, shader, NULL, 0));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, DSSetShader, context, NULL, NULL, 1));
}
