/* Expected values.  Microsoft's page of ID3D11DeviceContext::VSGetConstantBuffers (read
 * 2026-10-10: "Any returned interfaces will have their reference count incremented by one"):
 * the same pointer comes back, and its count goes up by one for each element returned.  The
 * slot count 14 is from memory, as in VSSetConstantBuffers.  The bindings are made with
 * rt_nulldev_bind, the contract's own bind, so this test does not depend on the Set body.
 * A slot never set reads NULL; a count of 0 does nothing; a NULL array with a count above 0 and
 * a range past the last slot are the notes' stops.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, VSGetConstantBuffers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11Buffer *a = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    ID3D11Buffer *b = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    rt_nulldev_bind(&c->vs_cb[7], a);
    rt_nulldev_bind(&c->vs_cb[13], b);
    ID3D11Buffer *one[1] = { b };
    ID3D11Buffer *three[3] = { b, b, b };

    T_CASE("a slot never set reads NULL");
    COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 0, 1, one);
    T_TRUE(one[0] == NULL);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a Get returns each bound buffer with one more reference");
    COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 6, 3, three);
    T_TRUE(three[0] == NULL);
    T_TRUE(three[1] == a);
    T_TRUE(three[2] == NULL);
    T_EQ(rt_com_refs(a), 3);
    COM_CALL(ID3D11Buffer, Release, three[1]);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get of the last slot returns it too");
    COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 13, 1, one);
    T_TRUE(one[0] == b);
    T_EQ(rt_com_refs(b), 3);
    COM_CALL(ID3D11Buffer, Release, one[0]);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a count of 0 with a NULL array does nothing");
    COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 7, 0, NULL);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 12, 3, three), "VSGetConstantBuffers for slots 12 to 14");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 0xffffffffu, 2, three), "VSGetConstantBuffers for slots 4294967295");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 0, 1, NULL), "VSGetConstantBuffers with a NULL array");
}
