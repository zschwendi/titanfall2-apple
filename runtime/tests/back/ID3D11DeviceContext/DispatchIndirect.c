/* The forward of ID3D11DeviceContext::DispatchIndirect through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the buffer
 * cross as their back objects, NULL as NULL; the offset crosses unchanged (argument 2 of the
 * recording back end is the value as 64 bits).  Rule 1: every stop comes before the back end is
 * reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, DispatchIndirect)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Buffer *args = T_BACK_NEW(device, &rt_nulldev_buffer_class);

    T_CASE("an indirect dispatch is handed on once, with its buffer and its offset");
    COM_CALL(ID3D11DeviceContext, DispatchIndirect, context, args, 16);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(DispatchIndirect), 1);
        T_EQ(T_BACK_ARG(DispatchIndirect, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(DispatchIndirect, 1), T_BACK_OF(args));
        T_EQ(T_BACK_ARG(DispatchIndirect, 2), 16);
    }

    /* every stop of the body, before the back end */
    ID3D11Buffer *not_a_buffer = (ID3D11Buffer *)T_BACK_NEW(device, &rt_nulldev_rtv_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, DispatchIndirect, context, NULL, 16));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, DispatchIndirect, context, not_a_buffer, 16));
}
