/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::GSSetShader (read 2026-10-10: "The method will hold a reference to the
 * interfaces passed in"): rt_nulldev_new gives the caller one reference, and the shader bound to
 * the slot holds one more.  The NULL read of a slot never set, the unbinding by NULL, and a count
 * of 0 doing no stop are the notes' rules; the stop for a count other than 0 is the notes' rule
 * too (no class instance can exist).  The slot is read through c->gs, the record the notes name.
 * None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, GSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11GeometryShader *a = rt_nulldev_new(device, &rt_nulldev_gs_class);
    ID3D11GeometryShader *b = rt_nulldev_new(device, &rt_nulldev_gs_class);
    char marker = 0;
    ID3D11ClassInstance const *inst[1] = { (ID3D11ClassInstance const *)&marker };

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->gs == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the shader and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, GSSetShader, context, a, NULL, 0);
    T_TRUE(c->gs == a);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over an occupied slot releases the old shader");
    COM_CALL(ID3D11DeviceContext, GSSetShader, context, b, NULL, 0);
    T_TRUE(c->gs == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL shader unbinds the slot and releases the shader");
    COM_CALL(ID3D11DeviceContext, GSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->gs == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("a count of 0 with a class-instance array binds the shader");
    COM_CALL(ID3D11DeviceContext, GSSetShader, context, a, inst, 0);
    T_TRUE(c->gs == a);
    T_EQ(rt_com_refs(a), 2);
    T_TRUE(inst[0] == (ID3D11ClassInstance const *)&marker);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, GSSetShader, context, b, NULL, 1), "GSSetShader with 1 class instances");
}
