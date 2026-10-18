/* The forward of ID3D11DeviceContext::DrawInstanced through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the first argument is the back
 * end's context (the back object of the front context); the four numbers cross as the game
 * passed them, in the order of the method's parameters.  The method has no stop, so there is
 * no T_BACK_STOPS_BEFORE. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, DrawInstanced)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("an instanced draw is handed on once, with its four numbers");
    COM_CALL(ID3D11DeviceContext, DrawInstanced, context, 6, 10, 2, 3);
    T_EQ(rt_com_calls(&rt_iface_ID3D11DeviceContext, "DrawInstanced"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DrawInstanced), 1);
        T_EQ(T_BACK_ARG(DrawInstanced, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(DrawInstanced, 1), 6);
        T_EQ(T_BACK_ARG(DrawInstanced, 2), 10);
        T_EQ(T_BACK_ARG(DrawInstanced, 3), 2);
        T_EQ(T_BACK_ARG(DrawInstanced, 4), 3);
    }

    T_CASE("every instanced draw is handed on, zeroes included");
    COM_CALL(ID3D11DeviceContext, DrawInstanced, context, 0, 0, 0, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DrawInstanced), 2);
        T_EQ(T_BACK_ARG(DrawInstanced, 1), 0);
        T_EQ(T_BACK_ARG(DrawInstanced, 4), 0);
        T_EQ(T_BACK_COUNT(Draw), 0);
    }
}
