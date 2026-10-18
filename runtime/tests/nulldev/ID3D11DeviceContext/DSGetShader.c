/* Expected values.  The reference rule is Microsoft's page of ID3D11DeviceContext::DSGetShader
 * (read 2026-10-10: "Any returned interfaces will have their reference count incremented by
 * one").  There is no domain shader to return: no class of this device presents
 * ID3D11DomainShader (finding 31, section 5), so the Get gives NULL, and the count of 0 written
 * through pNumClassInstances and the class-instance array left alone are the notes' rules.  The
 * device's count of 2 is from rt/nulldev.h (rt_nulldev_create: the caller's one and the
 * context's one; this test makes no other object that holds the device).  None of these values
 * comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, DSGetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11DomainShader *out;
    UINT num;
    int marker = 0;
    ID3D11ClassInstance *inst[2] = { (ID3D11ClassInstance *)&marker, NULL };

    T_CASE("a Get with no domain shader bound gives NULL and a count of 0 and leaves the class instances alone");
    T_TRUE(c->ds == NULL);
    out = (ID3D11DomainShader *)device;
    num = 7;
    COM_CALL(ID3D11DeviceContext, DSGetShader, context, &out, inst, &num);
    T_TRUE(out == NULL);
    T_EQ(num, 0);
    T_TRUE(inst[0] == (ID3D11ClassInstance *)&marker);
    T_TRUE(inst[1] == NULL);
    T_EQ(rt_com_refs(device), 2);

    T_CASE("a Get after a Set of NULL gives NULL and a count of 0");
    COM_CALL(ID3D11DeviceContext, DSSetShader, context, NULL, NULL, 0);
    out = (ID3D11DomainShader *)device;
    num = 7;
    COM_CALL(ID3D11DeviceContext, DSGetShader, context, &out, NULL, &num);
    T_TRUE(out == NULL);
    T_EQ(num, 0);
    T_EQ(rt_com_refs(device), 2);

    T_CASE("a NULL pNumClassInstances is accepted");
    out = (ID3D11DomainShader *)device;
    COM_CALL(ID3D11DeviceContext, DSGetShader, context, &out, NULL, NULL);
    T_TRUE(out == NULL);
    T_EQ(rt_com_refs(device), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, DSGetShader, context, NULL, inst, &num), "DSGetShader with a NULL out-pointer");
}
