/* The forward of ID3D11DeviceContext::OMSetBlendState through the seam (rt/d3dback.h).
 * Expected values: the rules of rt/d3dback.h, not the body.  Rule 2: the context and the blend
 * state cross as their back objects, NULL as NULL; the blend factor pointer and the sample mask
 * cross as the game passed them (a NULL factor stays NULL: the front's four ones are not
 * substituted for the back end).  Rule 1: every stop comes before the back end is reached.
 * The body has no RT_UNIMPLEMENTED; its stop is an object of the wrong class. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, OMSetBlendState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11BlendState *bs = T_BACK_NEW(device, &rt_nulldev_blend_class);
    FLOAT factor[4] = { 0.5f, 0.25f, 0.125f, 1.0f };

    T_CASE("a blend state is bound, its factor and mask are stored, and it is handed on as its back object");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, bs, factor, 0xFFFF);
    T_TRUE(c->blend == bs);
    T_TRUE(c->blend_factor[0] == 0.5f && c->blend_factor[1] == 0.25f && c->blend_factor[2] == 0.125f && c->blend_factor[3] == 1.0f);
    T_EQ(c->sample_mask, 0xFFFF);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetBlendState), 1);
        T_EQ(T_BACK_ARG(OMSetBlendState, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(OMSetBlendState, 1), T_BACK_OF(bs));
        T_TRUE(T_BACK_OF(bs) != T_BACK_PTR(bs));
        T_EQ(T_BACK_ARG(OMSetBlendState, 2), T_BACK_PTR(factor));
        T_EQ(T_BACK_ARG(OMSetBlendState, 3), 0xFFFF);
    }

    T_CASE("a NULL blend state and a NULL factor are handed on as NULL, the factor not substituted");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, NULL, NULL, 1);
    T_TRUE(c->blend == NULL);
    T_TRUE(c->blend_factor[0] == 1.0f && c->blend_factor[1] == 1.0f && c->blend_factor[2] == 1.0f && c->blend_factor[3] == 1.0f);
    T_EQ(c->sample_mask, 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(OMSetBlendState), 2);
        T_EQ(T_BACK_ARG(OMSetBlendState, 1), 0);
        T_EQ(T_BACK_ARG(OMSetBlendState, 2), 0);
        T_EQ(T_BACK_ARG(OMSetBlendState, 3), 1);
    }

    /* every stop of the body, before the back end */
    ID3D11BlendState *query_as_blend = (ID3D11BlendState *)T_BACK_NEW(device, &rt_nulldev_query_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, query_as_blend, factor, 0));
}
