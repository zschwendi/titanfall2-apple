/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::VSGetShader (read 2026-10-10: "Any returned interfaces will have their
 * reference count incremented by one"): the caller gets one more reference to the bound shader.
 * The NULL result for an unset shader, the 0 written to pNumClassInstances (no class instance is
 * ever bound) and the untouched ppClassInstances are the notes' rules.  None of these values comes
 * from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, VSGetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11VertexShader *a = rt_nulldev_new(device, &rt_nulldev_vs_class);
    ID3D11VertexShader *got = (ID3D11VertexShader *)1;
    ID3D11ClassInstance *inst[1] = { (ID3D11ClassInstance *)1 };
    UINT n = 99;

    T_CASE("a Get of a shader never set returns NULL");
    COM_CALL(ID3D11DeviceContext, VSGetShader, context, &got, NULL, &n);
    T_TRUE(got == NULL);
    T_EQ(n, 0);

    COM_CALL(ID3D11DeviceContext, VSSetShader, context, a, NULL, 0);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get hands out the bound shader with one more reference");
    got = NULL;
    COM_CALL(ID3D11DeviceContext, VSGetShader, context, &got, NULL, &n);
    T_TRUE(got == a);
    T_EQ(rt_com_refs(a), 3);

    T_CASE("a Get writes 0 to a non-NULL pNumClassInstances");
    n = 99;
    COM_CALL(ID3D11DeviceContext, VSGetShader, context, &got, NULL, &n);
    T_EQ(n, 0);

    T_CASE("a Get leaves ppClassInstances unwritten");
    COM_CALL(ID3D11DeviceContext, VSGetShader, context, &got, inst, &n);
    T_TRUE(inst[0] == (ID3D11ClassInstance *)1);
    T_TRUE(got == a);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, VSGetShader, context, NULL, NULL, &n), "VSGetShader with a NULL out-pointer");
}
