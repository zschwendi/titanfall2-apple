/* The forward of ID3D11DeviceContext::VSSetSamplers through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each sampler state
 * cross as their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's
 * own), a NULL array as NULL; StartSlot and NumSamplers as passed.  Rule 1: each of the body's
 * stops comes before the back end is reached.  The front records nothing for this bind (no Get
 * of it exists), so the holds are checked as unchanged: the body takes no reference. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, VSSetSamplers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11SamplerState *a = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState *b = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11Query *query = T_BACK_NEW(device, &rt_nulldev_query_class);
    ID3D11SamplerState const *three[3] = { a, NULL, b };
    LONG refs_a = rt_com_refs(a);

    T_CASE("three slots: each sampler crosses as its back object, NULL as NULL, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, VSSetSamplers, context, 4, 3, three);
    T_EQ(rt_com_refs(a), refs_a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(VSSetSamplers), 1);
        T_EQ(T_BACK_ARG(VSSetSamplers, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(VSSetSamplers, 1), 4);
        T_EQ(T_BACK_ARG(VSSetSamplers, 2), 3);
        T_TRUE(T_BACK_ARG(VSSetSamplers, 3) != 0 && T_BACK_ARG(VSSetSamplers, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(VSSetSamplers, 3), 3);
        T_EQ(T_BACK_ELEMENT(VSSetSamplers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(VSSetSamplers, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(VSSetSamplers, 3, 2), T_BACK_OF(b));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, VSSetSamplers, context, 2, 0, NULL);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(VSSetSamplers), 2);
        T_EQ(T_BACK_ARG(VSSetSamplers, 1), 2);
        T_EQ(T_BACK_ARG(VSSetSamplers, 2), 0);
        T_EQ(T_BACK_ARG(VSSetSamplers, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(VSSetSamplers, 3), 0);
    }

    T_CASE("the last slot");
    COM_CALL(ID3D11DeviceContext, VSSetSamplers, context, 15, 1, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(VSSetSamplers), 3);
        T_EQ(T_BACK_ELEMENT(VSSetSamplers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_COUNT(PSSetSamplers), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11SamplerState const *wrong[1] = { (ID3D11SamplerState const *)(void *)query };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, VSSetSamplers, context, 12, 5, three));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, VSSetSamplers, context, 0, 1, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, VSSetSamplers, context, 0, 1, wrong));
}
