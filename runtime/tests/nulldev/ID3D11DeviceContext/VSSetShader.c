/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::VSSetShader (read 2026-10-10: "The method will hold a reference to the
 * interfaces passed in"): rt_nulldev_new gives the caller one reference, and a bound shader holds
 * one more.  The NULL unbinding, the class-instance stop (the game creates no class linkage,
 * finding 31, section 4) and a NULL array with a count of 0 are the notes' rules.  None of these
 * values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, VSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11VertexShader *a = rt_nulldev_new(device, &rt_nulldev_vs_class);
    ID3D11VertexShader *b = rt_nulldev_new(device, &rt_nulldev_vs_class);

    T_CASE("a shader never set reads NULL");
    T_TRUE(c->vs == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the shader and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, VSSetShader, context, a, NULL, 0);
    T_TRUE(c->vs == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over a bound shader releases the old one");
    COM_CALL(ID3D11DeviceContext, VSSetShader, context, b, NULL, 0);
    T_TRUE(c->vs == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL shader unbinds and releases");
    COM_CALL(ID3D11DeviceContext, VSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->vs == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a Set with no class instances and a NULL array is accepted");
    COM_CALL(ID3D11DeviceContext, VSSetShader, context, a, NULL, 0);
    T_TRUE(c->vs == a);
    T_EQ(rt_com_refs(a), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, VSSetShader, context, b, NULL, 1), "class instances");
}
