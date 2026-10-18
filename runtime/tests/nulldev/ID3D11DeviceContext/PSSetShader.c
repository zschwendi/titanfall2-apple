/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::PSSetShader (read 2026-10-10: "The method will hold a reference to the
 * interfaces passed in"): rt_nulldev_new gives the caller one reference, and the bound shader
 * holds one more.  The NULL unbinding and the class-instance stop are the notes' rules: the game
 * creates no class linkage, so no class instance can exist.  None of these values comes from
 * the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11PixelShader *a = rt_nulldev_new(device, &rt_nulldev_ps_class);
    ID3D11PixelShader *b = rt_nulldev_new(device, &rt_nulldev_ps_class);

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->ps == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the shader and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, PSSetShader, context, a, NULL, 0);
    T_TRUE(c->ps == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over a bound shader releases the old one");
    COM_CALL(ID3D11DeviceContext, PSSetShader, context, b, NULL, 0);
    T_TRUE(c->ps == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL shader unbinds it and releases the shader");
    COM_CALL(ID3D11DeviceContext, PSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->ps == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a Set with no class instances ignores the instance array");
    COM_CALL(ID3D11DeviceContext, PSSetShader, context, a, NULL, 0);
    T_TRUE(c->ps == a);
    T_EQ(rt_com_refs(a), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetShader, context, b, NULL, 1), "PSSetShader with 1 class instances");
}
