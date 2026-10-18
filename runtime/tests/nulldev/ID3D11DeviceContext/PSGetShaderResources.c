/* Expected values.  The returned references come from Microsoft's page of
 * ID3D11DeviceContext::PSGetShaderResources (read 2026-10-10: "Any returned interfaces will have
 * their reference count incremented by one"): each view returned gives the caller one more
 * reference.  The slot count 128 is the header's number: the page names
 * D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT and not its value.  A slot never set or cleared by
 * a NULL element reads NULL, the NULL-array stop and a count of 0 doing nothing are the notes'
 * rules.  The bindings are made with PSSetShaderResources and the counts are the references that
 * Set holds; none of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSGetShaderResources)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11ShaderResourceView *a = rt_nulldev_new(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView *b = rt_nulldev_new(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView const *one[1] = { NULL };
    ID3D11ShaderResourceView *out[3];
    int marker;
    ID3D11ShaderResourceView *sentinel = (ID3D11ShaderResourceView *)&marker;

    T_CASE("a Get of a slot never set returns NULL");
    out[0] = sentinel;
    COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 5, 1, out);
    T_TRUE(out[0] == NULL);
    T_EQ(rt_com_refs(a), 1);

    one[0] = a;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 5, 1, one);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get returns the bound view and adds one reference for the caller");
    out[0] = sentinel;
    COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 5, 1, out);
    T_TRUE(out[0] == a);
    T_EQ(rt_com_refs(a), 3);
    rt_com_release(out[0]);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Get of a slot cleared by a NULL element returns NULL");
    one[0] = NULL;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 5, 1, one);
    T_EQ(rt_com_refs(a), 1);
    out[0] = sentinel;
    COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 5, 1, out);
    T_TRUE(out[0] == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Get reads several consecutive slots");
    one[0] = a;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 5, 1, one);
    out[0] = out[1] = out[2] = sentinel;
    COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 4, 3, out);
    T_TRUE(out[0] == NULL);
    T_TRUE(out[1] == a);
    T_TRUE(out[2] == NULL);
    T_EQ(rt_com_refs(a), 3);
    rt_com_release(out[1]);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("slot 127 is the last slot");
    one[0] = b;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 127, 1, one);
    out[0] = sentinel;
    COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 127, 1, out);
    T_TRUE(out[0] == b);
    T_EQ(rt_com_refs(b), 3);
    rt_com_release(out[0]);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a count of 0 with a NULL array does nothing");
    COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 2, 0, NULL);
    T_TRUE(c->ps_srv[5] == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 126, 3, out), "PSGetShaderResources for slots 126 to 128");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 0xffffffffu, 2, out), "PSGetShaderResources for slots 4294967295");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 200, 0, NULL), "PSGetShaderResources for slots 200 to 199");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSGetShaderResources, context, 0, 1, NULL), "PSGetShaderResources with a NULL array");
}
