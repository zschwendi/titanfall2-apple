/* The forward of ID3D11DeviceContext::Dispatch through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the first argument is the back
 * end's context (the back object of the front context); the three group counts cross as the
 * game passed them, unchanged.  The method has no stop, so there is no T_BACK_STOPS_BEFORE. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, Dispatch)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("a dispatch is handed on once, with its three numbers");
    COM_CALL(ID3D11DeviceContext, Dispatch, context, 8, 4, 1);
    T_EQ(rt_com_calls(&rt_iface_ID3D11DeviceContext, "Dispatch"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Dispatch), 1);
        T_EQ(T_BACK_ARG(Dispatch, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(Dispatch, 1), 8);
        T_EQ(T_BACK_ARG(Dispatch, 2), 4);
        T_EQ(T_BACK_ARG(Dispatch, 3), 1);
    }

    T_CASE("every dispatch is handed on, zeroes included");
    COM_CALL(ID3D11DeviceContext, Dispatch, context, 0, 0, 0);
    COM_CALL(ID3D11DeviceContext, Dispatch, context, 0xffffffffu, 7, 0x7fffffff);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Dispatch), 3);
        T_EQ(T_BACK_ARG(Dispatch, 1), 0xffffffffu);
        T_EQ(T_BACK_ARG(Dispatch, 3), 0x7fffffff);
        T_EQ(T_BACK_COUNT(DispatchIndirect), 0);
    }
}
