/* Expected values.  The reference rule is Microsoft's page of ID3D11DeviceContext::DSSetShader
 * (read 2026-10-10: "The method will hold a reference to the interfaces passed in").  No Set
 * with a shader can succeed here: no class of this device presents ID3D11DomainShader (finding
 * 31, section 5), so a non-NULL shader is a stop, and the device is passed for it only as an
 * address that is never used.  The device's count of 3 is from rt/nulldev.h: the caller's one and
 * the context's one (rt_nulldev_create), and the buffer's one (rt_nulldev_new holds one on the
 * device).  The NULL-shader Set doing nothing and the class-instance stop are the notes' rules.
 * None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, DSSetShader)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11Buffer *a = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    ID3D11Buffer const *one[1] = { a };
    ID3D11ClassInstance const *inst[2] = { NULL, NULL };

    T_CASE("a fresh context has no domain shader");
    T_TRUE(c->ds == NULL);
    T_EQ(rt_com_refs(device), 3);

    T_CASE("a Set of NULL with no class instances binds nothing and releases nothing");
    COM_CALL(ID3D11DeviceContext, VSSetConstantBuffers, context, 0, 1, one);
    T_EQ(rt_com_refs(a), 2);
    COM_CALL(ID3D11DeviceContext, DSSetShader, context, NULL, NULL, 0);
    T_TRUE(c->ds == NULL);
    T_TRUE(c->vs_cb[0] == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(device), 3);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, DSSetShader, context, (ID3D11DomainShader *)device, NULL, 0), "DSSetShader with a shader");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, DSSetShader, context, NULL, inst, 2), "DSSetShader with 2 class instances");
}
