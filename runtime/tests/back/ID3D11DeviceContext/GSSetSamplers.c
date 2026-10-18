/* The forward of ID3D11DeviceContext::GSSetSamplers through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each sampler state
 * cross as their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's
 * own), a NULL array as NULL; StartSlot and NumSamplers as passed.  Rule 1: each of the body's
 * stops comes before the back end is reached.  No older nulldev test exists for this method:
 * the front records nothing for it (no Get of this bind), so there is no front state to check. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, GSSetSamplers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11SamplerState *a = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState *b = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState *other = (ID3D11SamplerState *)T_BACK_NEW(device, &rt_nulldev_query_class);
    ID3D11SamplerState const *three[3] = { a, NULL, b };

    T_CASE("three slots: each sampler crosses as its back object, NULL as NULL, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, GSSetSamplers, context, 4, 3, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetSamplers), 1);
        T_EQ(T_BACK_ARG(GSSetSamplers, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(GSSetSamplers, 1), 4);
        T_EQ(T_BACK_ARG(GSSetSamplers, 2), 3);
        T_TRUE(T_BACK_ARG(GSSetSamplers, 3) != 0 && T_BACK_ARG(GSSetSamplers, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(GSSetSamplers, 3), 3);
        T_EQ(T_BACK_ELEMENT(GSSetSamplers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(GSSetSamplers, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(GSSetSamplers, 3, 2), T_BACK_OF(b));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, GSSetSamplers, context, 2, 0, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetSamplers), 2);
        T_EQ(T_BACK_ARG(GSSetSamplers, 1), 2);
        T_EQ(T_BACK_ARG(GSSetSamplers, 2), 0);
        T_EQ(T_BACK_ARG(GSSetSamplers, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(GSSetSamplers, 3), 0);
    }

    T_CASE("the last slot");
    COM_CALL(ID3D11DeviceContext, GSSetSamplers, context, 15, 1, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GSSetSamplers), 3);
        T_EQ(T_BACK_ARG(GSSetSamplers, 1), 15);
        T_EQ(T_BACK_ARG(GSSetSamplers, 2), 1);
        T_EQ(T_BACK_ELEMENT(GSSetSamplers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_COUNT(CSSetSamplers), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11SamplerState const *wrong[1] = { (ID3D11SamplerState const *)other };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetSamplers, context, 14, 3, three));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetSamplers, context, 0, 1, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GSSetSamplers, context, 0, 1, wrong));
}
