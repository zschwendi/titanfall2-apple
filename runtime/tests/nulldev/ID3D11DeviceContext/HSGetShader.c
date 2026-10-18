/* Expected values.  A Get returns NULL and a count of 0 because no class of this device presents
 * ID3D11HullShader (finding 31, section 5): the hull shader slot is always NULL, so there is no
 * interface to return and no class instance.  The reference rule is Microsoft's page of
 * ID3D11DeviceContext::HSGetShader (read 2026-10-10: "Any returned interfaces will have their
 * reference count incremented by one"): a NULL returned takes no reference, so the device's count
 * does not move, and no hull shader object exists whose count could go up.  The stop for a NULL
 * out-pointer, and the Set of NULL that precedes one case, are the notes'.  The class array is
 * not written because nothing is returned in it.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, HSGetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    LONG before = rt_com_refs(device);
    /* Sentinels: the Get must overwrite the shader and the count, and must leave the array alone.
     * The device is any live object here, and the body never reads or writes it. */
    ID3D11HullShader *hs = (ID3D11HullShader *)device;
    ID3D11ClassInstance *ci[2] = { (ID3D11ClassInstance *)device, (ID3D11ClassInstance *)device };
    UINT n = 7;

    T_CASE("a Get reads NULL, writes a count of 0 and leaves the class array alone");
    T_TRUE(c->hs == NULL);
    COM_CALL(ID3D11DeviceContext, HSGetShader, context, &hs, ci, &n);
    T_TRUE(hs == NULL);
    T_EQ(n, 0);
    T_TRUE(ci[0] == (ID3D11ClassInstance *)device);
    T_TRUE(ci[1] == (ID3D11ClassInstance *)device);
    T_EQ(rt_com_refs(device), before);

    T_CASE("a Set of NULL, then a Get, reads NULL and a count of 0");
    COM_CALL(ID3D11DeviceContext, HSSetShader, context, NULL, NULL, 0);
    hs = (ID3D11HullShader *)device;
    n = 7;
    COM_CALL(ID3D11DeviceContext, HSGetShader, context, &hs, NULL, &n);
    T_TRUE(hs == NULL);
    T_EQ(n, 0);
    T_EQ(rt_com_refs(device), before);

    T_CASE("a Get with a NULL count writes only the shader");
    hs = (ID3D11HullShader *)device;
    n = 7;
    COM_CALL(ID3D11DeviceContext, HSGetShader, context, &hs, ci, NULL);
    T_TRUE(hs == NULL);
    T_EQ(n, 7);
    T_TRUE(ci[0] == (ID3D11ClassInstance *)device);
    T_EQ(rt_com_refs(device), before);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, HSGetShader, context, NULL, ci, &n), "HSGetShader with a NULL out-pointer");
}
