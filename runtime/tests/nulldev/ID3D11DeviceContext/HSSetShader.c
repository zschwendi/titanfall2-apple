/* Expected values.  A hull shader slot reads NULL after creation, and a Set of NULL changes
 * nothing: both come from the notes, since no class of this device presents ID3D11HullShader
 * (finding 31, section 5), so the slot can never hold an object.  The reference rule is
 * Microsoft's page of ID3D11DeviceContext::HSSetShader (read 2026-10-10: "The method will hold
 * a reference to the interfaces passed in"); no hull shader object can exist to bind, so a Set
 * of a shader is checked as a stop, not as a count.  The stops, and the NumClassInstances rule,
 * are the notes'.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, HSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    /* The class array is a sentinel: the device is any live object, and the body never reads it. */
    ID3D11ClassInstance const *inst[2] = { (ID3D11ClassInstance const *)device, (ID3D11ClassInstance const *)device };
    LONG before = rt_com_refs(device);

    T_CASE("the hull shader reads NULL after creation");
    T_TRUE(c->hs == NULL);
    T_EQ(rt_com_refs(device), before);

    T_CASE("a Set of NULL with no class instances leaves it NULL");
    COM_CALL(ID3D11DeviceContext, HSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->hs == NULL);
    T_EQ(rt_com_refs(device), before);

    T_CASE("a Set of NULL with a class array and a count of 0 does nothing");
    COM_CALL(ID3D11DeviceContext, HSSetShader, context, NULL, inst, 0);
    T_TRUE(c->hs == NULL);
    T_TRUE(inst[0] == (ID3D11ClassInstance const *)device);
    T_TRUE(inst[1] == (ID3D11ClassInstance const *)device);
    T_EQ(rt_com_refs(device), before);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, HSSetShader, context, (ID3D11HullShader *)device, NULL, 0), "HSSetShader with a shader");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, HSSetShader, context, NULL, inst, 2), "HSSetShader with 2 class instances");
}
