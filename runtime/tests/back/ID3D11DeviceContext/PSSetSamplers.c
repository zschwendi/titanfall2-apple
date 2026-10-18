/* The forward of ID3D11DeviceContext::PSSetSamplers through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and each sampler state
 * cross as their back objects, NULL as NULL, in an array rebuilt on the stack (never the game's
 * own), a NULL array as NULL; StartSlot and NumSamplers as passed.  Rule 1: each of the body's
 * stops comes before the back end is reached.  What the front records (NdContext.ps_sampler, the
 * holds) is the same with and without a back end: runtime/tests/nulldev/ID3D11DeviceContext/
 * PSSetSamplers.c has those values and their sources. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, PSSetSamplers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11SamplerState *a = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState *b = T_BACK_NEW(device, &rt_nulldev_sampler_class);
    ID3D11Query *query = T_BACK_NEW(device, &rt_nulldev_query_class);
    ID3D11SamplerState const *three[3] = { a, NULL, b };

    T_CASE("three slots: each sampler crosses as its back object, NULL as NULL, in an array that is not the game's");
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 4, 3, three);
    T_TRUE(c->ps_sampler[4] == a && c->ps_sampler[5] == NULL && c->ps_sampler[6] == b);
    T_EQ(rt_com_refs(a), 2);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetSamplers), 1);
        T_EQ(T_BACK_ARG(PSSetSamplers, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(PSSetSamplers, 1), 4);
        T_EQ(T_BACK_ARG(PSSetSamplers, 2), 3);
        T_TRUE(T_BACK_ARG(PSSetSamplers, 3) != 0 && T_BACK_ARG(PSSetSamplers, 3) != T_BACK_PTR(three));
        T_EQ(T_BACK_ARRAY_LEN(PSSetSamplers, 3), 3);
        T_EQ(T_BACK_ELEMENT(PSSetSamplers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_ELEMENT(PSSetSamplers, 3, 1), 0);
        T_EQ(T_BACK_ELEMENT(PSSetSamplers, 3, 2), T_BACK_OF(b));
        T_TRUE(T_BACK_OF(a) != T_BACK_PTR(a) && T_BACK_OF(a) != T_BACK_OF(b));
    }

    T_CASE("a count of 0 with a NULL array is handed on with a NULL array");
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 2, 0, NULL);
    T_TRUE(c->ps_sampler[4] == a);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetSamplers), 2);
        T_EQ(T_BACK_ARG(PSSetSamplers, 1), 2);
        T_EQ(T_BACK_ARG(PSSetSamplers, 2), 0);
        T_EQ(T_BACK_ARG(PSSetSamplers, 3), 0);
        T_EQ(T_BACK_ARRAY_LEN(PSSetSamplers, 3), 0);
    }

    T_CASE("the last slot");
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 15, 1, three);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(PSSetSamplers), 3);
        T_EQ(T_BACK_ELEMENT(PSSetSamplers, 3, 0), T_BACK_OF(a));
        T_EQ(T_BACK_COUNT(VSSetSamplers), 0);
    }

    /* every stop of the body, and the wrong class of object, before the back end */
    ID3D11SamplerState const *wrong[1] = { (ID3D11SamplerState const *)(void *)query };
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 12, 5, three));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 0, 1, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 0, 1, wrong));
}
