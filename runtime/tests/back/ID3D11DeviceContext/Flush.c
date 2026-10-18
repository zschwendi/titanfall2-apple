/* The forward of ID3D11DeviceContext::Flush through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the first argument is the back end's
 * context (the back object of the front context).  The method takes no arguments and has no stop,
 * so there is no T_BACK_STOPS_BEFORE. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, Flush)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("a flush is handed on once, with the context's back object");
    COM_CALL(ID3D11DeviceContext, Flush, context);
    T_EQ(rt_com_calls(&rt_iface_ID3D11DeviceContext, "Flush"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Flush), 1);
        T_EQ(T_BACK_ARG(Flush, 0), T_BACK_OF(context));
    }

    T_CASE("a second flush is handed on as well");
    COM_CALL(ID3D11DeviceContext, Flush, context);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Flush), 2);
        T_EQ(T_BACK_ARG(Flush, 0), T_BACK_OF(context));
    }
}
