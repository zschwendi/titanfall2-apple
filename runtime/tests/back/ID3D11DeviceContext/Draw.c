/* The forward of ID3D11DeviceContext::Draw through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the first argument is the back
 * end's context (the back object of the front context); the two numbers cross as the game
 * passed them.  The method has no stop, so there is no T_BACK_STOPS_BEFORE. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, Draw)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("a draw is handed on once, with its two numbers");
    COM_CALL(ID3D11DeviceContext, Draw, context, 3, 7);
    T_EQ(rt_com_calls(&rt_iface_ID3D11DeviceContext, "Draw"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Draw), 1);
        T_EQ(T_BACK_ARG(Draw, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(Draw, 1), 3);
        T_EQ(T_BACK_ARG(Draw, 2), 7);
    }

    T_CASE("every draw is handed on, zeroes and the largest count included");
    COM_CALL(ID3D11DeviceContext, Draw, context, 0, 0);
    COM_CALL(ID3D11DeviceContext, Draw, context, 0xffffffffu, 0xffffffffu);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Draw), 3);
        T_EQ(T_BACK_ARG(Draw, 1), 0xffffffffu);
        T_EQ(T_BACK_ARG(Draw, 2), 0xffffffffu);
        T_EQ(T_BACK_COUNT(DrawIndexed), 0);
    }
}
