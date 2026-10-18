/* The forward of ID3D11DeviceContext::IASetInputLayout through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the layout
 * cross as their back objects, NULL as NULL.  Rule 1: every stop comes before the back end is
 * reached.  The layout is an argument, so an object of another class is a stop too. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, IASetInputLayout)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11InputLayout *layout = T_BACK_NEW(device, &rt_nulldev_inputlayout_class);

    T_CASE("a layout is bound, and handed on as its back object");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, layout);
    T_TRUE(c->input_layout == layout);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetInputLayout), 1);
        T_EQ(T_BACK_ARG(IASetInputLayout, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(IASetInputLayout, 1), T_BACK_OF(layout));
        T_TRUE(T_BACK_OF(layout) != T_BACK_PTR(layout));
    }

    T_CASE("a NULL layout is handed on as NULL");
    COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, NULL);
    T_TRUE(c->input_layout == NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetInputLayout), 2);
        T_EQ(T_BACK_ARG(IASetInputLayout, 1), 0);
    }

    /* every stop of the body, before the back end */
    ID3D11InputLayout *query_as_layout = (ID3D11InputLayout *)T_BACK_NEW(device, &rt_nulldev_query_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetInputLayout, context, query_as_layout));
}
