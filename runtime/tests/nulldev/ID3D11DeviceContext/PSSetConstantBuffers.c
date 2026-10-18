/* Expected values.  The reference counts and the NULL reads come from Microsoft's page of
 * ID3D11DeviceContext::PSSetConstantBuffers (read 2026-10-10: "The method will hold a reference
 * to the interfaces passed in"): rt_nulldev_new gives the caller one reference, and each slot
 * that holds a buffer holds one more.  The slot count 14 is from memory: the page names
 * D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT and not its value.  The NULL element
 * unbinding, the NULL-array stop, the range stop and a count of 0 doing nothing are the notes'
 * rules.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSSetConstantBuffers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11Buffer *a = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    ID3D11Buffer *b = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    ID3D11Buffer const *one[1] = { NULL }, *two[2] = { NULL, NULL }, *three[3] = { NULL, NULL, NULL };

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->ps_cb[0] == NULL);
    T_TRUE(c->ps_cb[5] == NULL);
    T_TRUE(c->vs_cb[5] == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the buffer and holds one reference to it");
    one[0] = a;
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 5, 1, one);
    T_TRUE(c->ps_cb[5] == a);
    T_TRUE(c->ps_cb[4] == NULL);
    T_TRUE(c->ps_cb[6] == NULL);
    T_TRUE(c->vs_cb[5] == NULL);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over an occupied slot releases the old buffer");
    one[0] = b;
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 5, 1, one);
    T_TRUE(c->ps_cb[5] == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL element unbinds its slot and releases the buffer");
    one[0] = NULL;
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 5, 1, one);
    T_TRUE(c->ps_cb[5] == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("NumBuffers above 1 fills consecutive slots");
    two[0] = a;
    two[1] = b;
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 2, 2, two);
    T_TRUE(c->ps_cb[2] == a);
    T_TRUE(c->ps_cb[3] == b);
    T_TRUE(c->ps_cb[4] == NULL);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("slot 13 is the last slot");
    one[0] = b;
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 13, 1, one);
    T_TRUE(c->ps_cb[13] == b);
    T_EQ(rt_com_refs(b), 3);

    T_CASE("a count of 0 with a NULL array does nothing");
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 2, 0, NULL);
    T_TRUE(c->ps_cb[2] == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 3);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 12, 3, three), "PSSetConstantBuffers for slots 12 to 14");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 0xffffffffu, 2, two), "PSSetConstantBuffers for slots 4294967295");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 0, 1, NULL), "PSSetConstantBuffers with a NULL array");
}
