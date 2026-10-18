/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::PSGetConstantBuffers (read 2026-10-10: "Any returned interfaces will have
 * their reference count incremented by one"): each buffer handed out gains one reference for the
 * caller, which the test releases with rt_com_release as a game would.  The bindings are made
 * with PSSetConstantBuffers, whose page says the method holds one reference to each buffer.  The
 * slot count 14 is from memory: the page names D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
 * and not its value.  The NULL-array stop and a count of 0 doing nothing are the notes' rules.
 * None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSGetConstantBuffers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11Buffer *a = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    ID3D11Buffer *b = rt_nulldev_new(device, &rt_nulldev_buffer_class);
    ID3D11Buffer const *two[2] = { NULL, NULL };
    ID3D11Buffer *out[3] = { NULL, NULL, NULL };

    /* Bind a at slot 2 and b at slot 3 through the Set method, then a: 2 refs, b: 2 refs. */
    two[0] = a;
    two[1] = b;
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 2, 2, two);
    /* Caller references: a and b each have one from rt_nulldev_new, plus one from the bind. */

    T_CASE("a Get of a never-set slot returns NULL");
    out[0] = a;
    COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 0, 1, out);
    T_TRUE(out[0] == NULL);
    T_EQ(rt_com_refs(a), 2);
    T_TRUE(c->ps_cb[0] == NULL);

    T_CASE("a Get returns the bound buffer with one more reference");
    out[0] = NULL;
    COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 2, 1, out);
    T_TRUE(out[0] == a);
    T_EQ(rt_com_refs(a), 3);
    rt_com_release(out[0]);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get of a cleared slot returns NULL");
    ID3D11Buffer const *none[1] = { NULL };
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 3, 1, none);
    out[0] = b;
    COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 3, 1, out);
    T_TRUE(out[0] == NULL);
    T_TRUE(c->ps_cb[3] == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("NumBuffers above 1 returns consecutive slots");
    out[0] = out[1] = out[2] = NULL;
    COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 1, 3, out);
    T_TRUE(out[0] == NULL);
    T_TRUE(out[1] == a);
    T_TRUE(out[2] == NULL);
    T_EQ(rt_com_refs(a), 3);
    rt_com_release(out[1]);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("slot 13 is the last slot");
    ID3D11Buffer const *one[1] = { b };
    COM_CALL(ID3D11DeviceContext, PSSetConstantBuffers, context, 13, 1, one);
    T_TRUE(c->ps_cb[13] == b);
    T_EQ(rt_com_refs(b), 2);
    out[0] = NULL;
    COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 13, 1, out);
    T_TRUE(out[0] == b);
    T_EQ(rt_com_refs(b), 3);
    rt_com_release(out[0]);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a count of 0 with a NULL array does nothing");
    COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 2, 0, NULL);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 12, 3, out), "PSGetConstantBuffers for slots 12 to 14");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 0xffffffffu, 2, out), "PSGetConstantBuffers for slots 4294967295");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetConstantBuffers, context, 0, 1, NULL), "PSGetConstantBuffers with a NULL array");
}
