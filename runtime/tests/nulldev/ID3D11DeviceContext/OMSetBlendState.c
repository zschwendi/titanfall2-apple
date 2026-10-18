/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::OMSetBlendState (read 2026-10-10: "The method will hold a reference to the
 * interfaces passed in"): rt_nulldev_new gives the caller one reference, and the bound blend state
 * holds one more.  The NULL blend factor {1, 1, 1, 1} is the page's sentence "If you pass NULL, the
 * runtime uses or stores a blend factor equal to { 1, 1, 1, 1 }".  The sample mask stored as given,
 * and a NULL blend state unbinding, are the notes' rules.  The factor values 0.25, 0.5, 0.75 and
 * 0.125 are test inputs; they are exact in binary floating point, so == is exact.  None of these
 * values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, OMSetBlendState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11BlendState *a = rt_nulldev_new(device, &rt_nulldev_blend_class);
    ID3D11BlendState *b = rt_nulldev_new(device, &rt_nulldev_blend_class);
    const FLOAT factor[4] = { 0.25f, 0.5f, 0.75f, 0.125f };

    T_CASE("a new context binds no blend state");
    T_TRUE(c->blend == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the blend state and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, a, factor, 0xffffffffu);
    T_TRUE(c->blend == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("setting the bound state again keeps its count");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, a, factor, 0xffffffffu);
    T_TRUE(c->blend == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over a bound state releases the old one");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, b, factor, 0x3u);
    T_TRUE(c->blend == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a blend factor is stored as given");
    T_TRUE(c->blend_factor[0] == 0.25f);
    T_TRUE(c->blend_factor[1] == 0.5f);
    T_TRUE(c->blend_factor[2] == 0.75f);
    T_TRUE(c->blend_factor[3] == 0.125f);

    T_CASE("a NULL blend factor stores {1, 1, 1, 1}");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, b, NULL, 0xffffffffu);
    T_TRUE(c->blend_factor[0] == 1.0f);
    T_TRUE(c->blend_factor[1] == 1.0f);
    T_TRUE(c->blend_factor[2] == 1.0f);
    T_TRUE(c->blend_factor[3] == 1.0f);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a sample mask is stored as given");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, b, factor, 0x5u);
    T_TRUE(c->sample_mask == 0x5u);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL blend state unbinds and releases the old one");
    COM_CALL(ID3D11DeviceContext, OMSetBlendState, context, NULL, factor, 0xffffffffu);
    T_TRUE(c->blend == NULL);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 1);
}
