/* Expected values.  The reference count of a bound state comes from Microsoft's page of
 * ID3D11DeviceContext::OMSetDepthStencilState (read 2026-10-10: "The method will hold a reference
 * to the interfaces passed in"): rt_nulldev_new gives the caller one reference, and the bound state
 * holds one more.  The stencil reference is the value the test passes, stored as given.  The
 * defaults of the other bindings (nothing bound, sample mask 0xffffffff, blend factor 1,1,1,1, no
 * viewport) are those of ID3D11DeviceContext::ClearState's page, as the notes give them.  None of
 * these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, OMSetDepthStencilState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11DepthStencilState *a = rt_nulldev_new(device, &rt_nulldev_depthstencil_class);
    ID3D11DepthStencilState *b = rt_nulldev_new(device, &rt_nulldev_depthstencil_class);

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->depth_stencil == NULL);
    T_EQ(c->stencil_ref, 0);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the state and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, a, 5);
    T_TRUE(c->depth_stencil == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set stores the stencil reference");
    /* The same state again: it is bound once, so it still holds one reference. */
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, a, 0xffffffffu);
    T_TRUE(c->depth_stencil == a);
    T_EQ(c->stencil_ref, 0xffffffffu);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over a bound state releases the old state");
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, b, 7);
    T_TRUE(c->depth_stencil == b);
    T_EQ(c->stencil_ref, 7);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL state unbinds and releases the old state");
    COM_CALL(ID3D11DeviceContext, OMSetDepthStencilState, context, NULL, 0);
    T_TRUE(c->depth_stencil == NULL);
    T_EQ(c->stencil_ref, 0);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a Set leaves the other bindings alone");
    T_TRUE(c->blend == NULL);
    T_TRUE(c->rasterizer == NULL);
    T_TRUE(c->dsv == NULL);
    T_TRUE(c->vs == NULL);
    T_EQ(c->nviewports, 0);
    T_EQ(c->sample_mask, 0xffffffffu);
    T_TRUE(c->blend_factor[0] == 1.0f && c->blend_factor[1] == 1.0f &&
           c->blend_factor[2] == 1.0f && c->blend_factor[3] == 1.0f);
}
