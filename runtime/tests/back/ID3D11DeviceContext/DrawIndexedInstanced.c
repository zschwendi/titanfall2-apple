/* The forward of ID3D11DeviceContext::DrawIndexedInstanced through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the first argument is the back
 * end's context (the back object of the front context); the five numbers cross as the game
 * passed them, the signed BaseVertexLocation sign-extended by the recording back end
 * (back_record.c keeps an INT as int64_t).  The method has no stop, so there is no
 * T_BACK_STOPS_BEFORE. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, DrawIndexedInstanced)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("a draw is handed on once, with its five numbers");
    COM_CALL(ID3D11DeviceContext, DrawIndexedInstanced, context, 36, 4, 6, -2, 1);
    T_EQ(rt_com_calls(&rt_iface_ID3D11DeviceContext, "DrawIndexedInstanced"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DrawIndexedInstanced), 1);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 1), 36);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 2), 4);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 3), 6);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 4), (uint64_t)(int64_t)-2);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 5), 1);
    }

    T_CASE("every draw is handed on, zeroes included");
    COM_CALL(ID3D11DeviceContext, DrawIndexedInstanced, context, 0, 0, 0, 0, 0);
    COM_CALL(ID3D11DeviceContext, DrawIndexedInstanced, context, 0xffffffffu, 0, 7, 0x7fffffff, 0xffffffffu);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DrawIndexedInstanced), 3);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 1), 0xffffffffu);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 4), 0x7fffffff);
        T_EQ(T_BACK_ARG(DrawIndexedInstanced, 5), 0xffffffffu);
        T_EQ(T_BACK_COUNT(DrawIndexed), 0);
    }
}
