/* The forward of ID3D11DeviceContext::PSSetConstantBuffers through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each buffer cross as
 * their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's own), a
 * NULL array as NULL; StartSlot and NumBuffers as passed.  Rule 1: each of the body's stops comes
 * before the back end is reached.  What the front records (NdContext.ps_cb, the holds) is the
 * same with and without a back end: runtime/tests/nulldev/ID3D11DeviceContext/PSSetConstantBuffers.c
 * has those values and their sources. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, PSSetConstantBuffers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11Buffer *a = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    ID3D11Buffer *b = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    ID3D11SamplerState *sampler = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11Buffer const *three[3] = { a, NULL, b };

    T_CASE("three slots: each buffer crosses as its back object, NULL as NULL, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 4, 3, three);
    T_TRUE(c->ps_cb[4] == a && c->ps_cb[5] == NULL && c->ps_cb[6] == b);
    T_EQ(rt_com_refs(a), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetConstantBuffers), 1);
        T_EQ(T_BACK_ARG(PSSetConstantBuffers, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(PSSetConstantBuffers, 1), 4);
        T_EQ(T_BACK_ARG(PSSetConstantBuffers, 2), 3);
        T_TRUE(T_BACK_ARG(PSSetConstantBuffers, 3) != 0 && T_BACK_ARG(PSSetConstantBuffers, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(PSSetConstantBuffers, 3), 3);
        T_EQ(T_BACK_ELEMENT(PSSetConstantBuffers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(PSSetConstantBuffers, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(PSSetConstantBuffers, 3, 2), T_BACK_OF(b));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 2, 0, NULL);
    T_TRUE(c->ps_cb[4] == a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetConstantBuffers), 2);
        T_EQ(T_BACK_ARG(PSSetConstantBuffers, 1), 2);
        T_EQ(T_BACK_ARG(PSSetConstantBuffers, 2), 0);
        T_EQ(T_BACK_ARG(PSSetConstantBuffers, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(PSSetConstantBuffers, 3), 0);
    }

    T_CASE("the last slot");
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 13, 1, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetConstantBuffers), 3);
        T_EQ(T_BACK_ELEMENT(PSSetConstantBuffers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_COUNT(GSSetConstantBuffers), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11Buffer const *wrong[1] = { (ID3D11Buffer const *)sampler };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 12, 3, three));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 0, 1, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 0, 1, wrong));
}
