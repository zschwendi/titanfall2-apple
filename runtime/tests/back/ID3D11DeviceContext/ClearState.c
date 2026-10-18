/* The forward of ID3D11DeviceContext::ClearState through the seam (rt/d3dback.h).
 * Expected values: the seam's rules and the page the body cites (ClearState resets the context to
 * its defaults: every resource slot NULL), not the body.  Rule 2: the first argument is the back
 * end's context, the same object before and after the call.  The method has no stop, so there is
 * no T_BACK_STOPS_BEFORE.  The front's own result (the slot NULL, the buffer's one reference) is
 * the same with and without a back end. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, ClearState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11Buffer *a = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    ID3D11Buffer const *one[1] = { a };

    T_CASE("a bound buffer is released by ClearState; the context's back object is the same before and after");
    COM_CALL(ID3D11DeviceContext, VSSetConstantBuffers, context, 0, 1, one);
    T_TRUE(c->vs_cb[0] == a);
    T_EQ(rt_com_refs(a), 2);
    uint64_t before = T_BACK_OF(context);
    COM_CALL(ID3D11DeviceContext, ClearState, context);
    T_TRUE(c->vs_cb[0] == NULL);
    T_EQ(rt_com_refs(a), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ClearState), 1);
        T_EQ(T_BACK_ARG(ClearState, 0), before);
        T_EQ(T_BACK_ARG(ClearState, 0), T_BACK_OF(context));
    }
}
