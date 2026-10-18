/* Expected values.  The reference counts and the NULL reads come from Microsoft's page of
 * ID3D11DeviceContext::PSSetSamplers (read 2026-10-10: "The method will hold a reference to the
 * interfaces passed in"): rt_nulldev_new gives the caller one reference, and each slot that holds
 * a sampler holds one more.  The slot count 16 is the header's number (D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT).
 * The NULL element unbinding, the NULL-array stop, the range stop and a count of 0 doing nothing
 * are the notes' rules.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSSetSamplers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11SamplerState *a = rt_nulldev_new(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState *b = rt_nulldev_new(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState const *one[1] = { NULL }, *two[2] = { NULL, NULL }, *three[3] = { NULL, NULL, NULL };

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->ps_sampler[0] == NULL);
    T_TRUE(c->ps_sampler[5] == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the sampler and holds one reference to it");
    one[0] = a;
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 5, 1, one);
    T_TRUE(c->ps_sampler[5] == a);
    T_TRUE(c->ps_sampler[4] == NULL);
    T_TRUE(c->ps_sampler[6] == NULL);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over an occupied slot releases the old sampler");
    one[0] = b;
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 5, 1, one);
    T_TRUE(c->ps_sampler[5] == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL element unbinds its slot and releases the sampler");
    one[0] = NULL;
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 5, 1, one);
    T_TRUE(c->ps_sampler[5] == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("NumSamplers above 1 fills consecutive slots");
    two[0] = a;
    two[1] = b;
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 2, 2, two);
    T_TRUE(c->ps_sampler[2] == a);
    T_TRUE(c->ps_sampler[3] == b);
    T_TRUE(c->ps_sampler[4] == NULL);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("slot 15 is the last slot");
    one[0] = b;
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 15, 1, one);
    T_TRUE(c->ps_sampler[15] == b);
    T_EQ(rt_com_refs(b), 3);

    T_CASE("a count of 0 with a NULL array does nothing");
    COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 2, 0, NULL);
    T_TRUE(c->ps_sampler[2] == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 3);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 14, 3, three), "PSSetSamplers for slots 14 to 16");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 0xffffffffu, 2, two), "PSSetSamplers for slots 4294967295");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetSamplers, context, 0, 1, NULL), "PSSetSamplers with a NULL array");
}
