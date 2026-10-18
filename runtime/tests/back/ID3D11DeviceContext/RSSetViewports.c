/* The forward of ID3D11DeviceContext::RSSetViewports through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context crosses as its
 * back object, the count as a value, the viewport array by pointer (the game's own memory).
 * Rule 1: every stop comes before the back end is reached.  The stops are: more than 16
 * viewports (ND_VIEWPORTS), and a NULL array with a count. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, RSSetViewports)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    D3D11_VIEWPORT v[2] = { { 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f }, { 10.0f, 20.0f, 320.0f, 240.0f, 0.0f, 1.0f } };

    T_CASE("two viewports are stored, and handed on with their count and the game's array");
    COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 2, v);
    T_EQ(c->nviewports, 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(RSSetViewports), 1);
        T_EQ(T_BACK_ARG(RSSetViewports, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(RSSetViewports, 1), 2);
        T_EQ(T_BACK_ARG(RSSetViewports, 2), T_BACK_PTR(v));
    }

    T_CASE("no viewports are handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 0, NULL);
    T_EQ(c->nviewports, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(RSSetViewports), 2);
        T_EQ(T_BACK_ARG(RSSetViewports, 1), 0);
        T_EQ(T_BACK_ARG(RSSetViewports, 2), 0);
    }

    /* every stop of the body, before the back end */
    D3D11_VIEWPORT v17[17];
    memset(v17, 0, sizeof v17);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 17, v17));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, RSSetViewports, context, 1, NULL));
}
