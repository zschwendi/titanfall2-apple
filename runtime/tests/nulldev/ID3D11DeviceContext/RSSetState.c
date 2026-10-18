/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::RSSetState (read 2026-10-10: "The method will hold a reference to the
 * interfaces passed in"): rt_nulldev_new gives the caller one reference, and the bound state
 * holds one more.  The NULL-unbinds rule and the never-set NULL read are the notes' rules.
 * None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, RSSetState)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11RasterizerState *a = rt_nulldev_new(device, &rt_nulldev_rasterizer_class);
    ID3D11RasterizerState *b = rt_nulldev_new(device, &rt_nulldev_rasterizer_class);

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->rasterizer == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the rasterizer state and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, RSSetState, context, a);
    T_TRUE(c->rasterizer == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over an occupied slot releases the old state");
    COM_CALL(ID3D11DeviceContext, RSSetState, context, b);
    T_TRUE(c->rasterizer == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL argument unbinds and releases the state");
    COM_CALL(ID3D11DeviceContext, RSSetState, context, NULL);
    T_TRUE(c->rasterizer == NULL);
    T_EQ(rt_com_refs(b), 1);
}
