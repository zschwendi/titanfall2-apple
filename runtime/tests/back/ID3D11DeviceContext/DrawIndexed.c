/* The forward of ID3D11DeviceContext::DrawIndexed through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the first argument is the back
 * end's context (the back object of the front context); the three numbers cross as the game
 * passed them, the signed one sign-extended by the recording back end (back_record.c keeps an
 * INT as int64_t).  The method has no stop, so there is no T_BACK_STOPS_BEFORE. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, DrawIndexed)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("a draw is handed on once, with its three numbers");
    COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 36, 6, -2);
    T_EQ(rt_com_calls(&rt_iface_ID3D11DeviceContext, "DrawIndexed"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DrawIndexed), 1);
        T_EQ(T_BACK_ARG(DrawIndexed, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(DrawIndexed, 1), 36);
        T_EQ(T_BACK_ARG(DrawIndexed, 2), 6);
        T_EQ(T_BACK_ARG(DrawIndexed, 3), (uint64_t)(int64_t)-2);
    }

    T_CASE("every draw is handed on, zeroes included");
    COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 0, 0, 0);
    COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 0xffffffffu, 7, 0x7fffffff);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DrawIndexed), 3);
        T_EQ(T_BACK_ARG(DrawIndexed, 1), 0xffffffffu);
        T_EQ(T_BACK_ARG(DrawIndexed, 3), 0x7fffffff);
        T_EQ(T_BACK_COUNT(Draw), 0);
    }
}
