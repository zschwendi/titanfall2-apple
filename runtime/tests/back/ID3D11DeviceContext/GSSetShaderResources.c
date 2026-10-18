/* The forward of ID3D11DeviceContext::GSSetShaderResources through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each view cross as
 * their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's own), a
 * NULL array as NULL; StartSlot and NumViews as passed.  Rule 1: each of the body's stops comes
 * before the back end is reached.  The front records nothing for this bind (no Get exists), so
 * the only front-side check is that the views are not held. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, GSSetShaderResources)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11ShaderResourceView *a = T_BACK_NEW(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView *b = T_BACK_NEW(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView const *three[3] = { a, NULL, b };

    T_CASE("three slots: each view crosses as its back object, NULL as NULL, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, GSSetShaderResources, context, 4, 3, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetShaderResources), 1);
        T_EQ(T_BACK_ARG(GSSetShaderResources, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(GSSetShaderResources, 1), 4);
        T_EQ(T_BACK_ARG(GSSetShaderResources, 2), 3);
        T_TRUE(T_BACK_ARG(GSSetShaderResources, 3) != 0 && T_BACK_ARG(GSSetShaderResources, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(GSSetShaderResources, 3), 3);
        T_EQ(T_BACK_ELEMENT(GSSetShaderResources, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(GSSetShaderResources, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(GSSetShaderResources, 3, 2), T_BACK_OF(b));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, GSSetShaderResources, context, 2, 0, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetShaderResources), 2);
        T_EQ(T_BACK_ARG(GSSetShaderResources, 1), 2);
        T_EQ(T_BACK_ARG(GSSetShaderResources, 2), 0);
        T_EQ(T_BACK_ARG(GSSetShaderResources, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(GSSetShaderResources, 3), 0);
    }

    T_CASE("the last slot");
    COM_CALL(ID3D11DeviceContext, GSSetShaderResources, context, 127, 1, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetShaderResources), 3);
        T_EQ(T_BACK_ARG(GSSetShaderResources, 1), 127);
        T_EQ(T_BACK_ELEMENT(GSSetShaderResources, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_COUNT(VSSetShaderResources), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11ShaderResourceView const *wrong[1] = { (ID3D11ShaderResourceView const *)T_BACK_NEW(device, &rt_nulldev_query_class) };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetShaderResources, context, 127, 2, three));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetShaderResources, context, 0, 1, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetShaderResources, context, 0, 1, wrong));
}
