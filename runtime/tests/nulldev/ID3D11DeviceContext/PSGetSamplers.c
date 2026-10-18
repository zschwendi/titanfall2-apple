/* Expected values.  The returned pointers and the reference counts come from Microsoft's page of
 * ID3D11DeviceContext::PSGetSamplers (read 2026-10-10: "Any returned interfaces will have their
 * reference count incremented by one"): rt_nulldev_new gives the caller one reference, the context
 * holds one more while the sampler is bound, and each Get hands the caller one more.  The slot
 * count 16 is the header's number (D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT).  The NULL-array stop and
 * the range stop are the notes' rules.  The bindings are made with rt_nulldev_bind, which is the
 * Set's own bind, so this test does not depend on PSSetSamplers.  None of these values comes from
 * the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSGetSamplers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11SamplerState *a = rt_nulldev_new(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState *b = rt_nulldev_new(device, &rt_nulldev_sampler_class);
    ID3D11SamplerState *out[2] = { NULL, NULL }, *three[3] = { NULL, NULL, NULL };

    rt_nulldev_bind(&c->ps_sampler[3], a);
    rt_nulldev_bind(&c->ps_sampler[4], b);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a Get returns the bound sampler with one more reference");
    COM_CALL(ID3D11DeviceContext, PSGetSamplers, context, 3, 1, out);
    T_TRUE(out[0] == a);
    T_EQ(rt_com_refs(a), 3);
    T_EQ(rt_com_refs(b), 2);
    COM_CALL(ID3D11SamplerState, Release, out[0]);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get of a slot never set returns NULL");
    out[0] = b;
    COM_CALL(ID3D11DeviceContext, PSGetSamplers, context, 0, 1, out);
    T_TRUE(out[0] == NULL);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a Get of a range returns each slot in order");
    COM_CALL(ID3D11DeviceContext, PSGetSamplers, context, 3, 2, out);
    T_TRUE(out[0] == a);
    T_TRUE(out[1] == b);
    T_EQ(rt_com_refs(a), 3);
    T_EQ(rt_com_refs(b), 3);
    COM_CALL(ID3D11SamplerState, Release, out[0]);
    COM_CALL(ID3D11SamplerState, Release, out[1]);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetSamplers, context, 14, 3, three), "PSGetSamplers for slots 14 to 16");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetSamplers, context, 0xffffffffu, 2, out), "PSGetSamplers for slots 4294967295");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetSamplers, context, 0, 1, NULL), "PSGetSamplers with a NULL array");
}
