/* The forward of ID3D11DeviceContext::GSSetConstantBuffers through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each buffer cross as
 * their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's own), a
 * NULL array as NULL; StartSlot and NumBuffers as passed.  Rule 1: each of the body's stops comes
 * before the back end is reached.  The front keeps nothing for this bind (no Get of it exists),
 * so the only state checked is the reference each buffer keeps: none, since nothing is bound. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, GSSetConstantBuffers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Buffer *a = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    ID3D11Buffer *b = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    ID3D11SamplerState *sampler = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11Buffer const *three[3] = { a, NULL, b };

    T_CASE("three slots: each buffer crosses as its back object, NULL as NULL, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, GSSetConstantBuffers, context, 4, 3, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetConstantBuffers), 1);
        T_EQ(T_BACK_ARG(GSSetConstantBuffers, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(GSSetConstantBuffers, 1), 4);
        T_EQ(T_BACK_ARG(GSSetConstantBuffers, 2), 3);
        T_TRUE(T_BACK_ARG(GSSetConstantBuffers, 3) != 0 && T_BACK_ARG(GSSetConstantBuffers, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(GSSetConstantBuffers, 3), 3);
        T_EQ(T_BACK_ELEMENT(GSSetConstantBuffers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(GSSetConstantBuffers, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(GSSetConstantBuffers, 3, 2), T_BACK_OF(b));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, GSSetConstantBuffers, context, 2, 0, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetConstantBuffers), 2);
        T_EQ(T_BACK_ARG(GSSetConstantBuffers, 1), 2);
        T_EQ(T_BACK_ARG(GSSetConstantBuffers, 2), 0);
        T_EQ(T_BACK_ARG(GSSetConstantBuffers, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(GSSetConstantBuffers, 3), 0);
    }

    T_CASE("the last slot");
    COM_CALL(ID3D11DeviceContext, GSSetConstantBuffers, context, 13, 1, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetConstantBuffers), 3);
        T_EQ(T_BACK_ELEMENT(GSSetConstantBuffers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_COUNT(PSSetConstantBuffers), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11Buffer const *wrong[1] = { (ID3D11Buffer const *)sampler };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetConstantBuffers, context, 12, 3, three));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetConstantBuffers, context, 0, 1, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetConstantBuffers, context, 0, 1, wrong));
}
