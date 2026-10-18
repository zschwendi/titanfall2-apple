/* Expected values.  The reference counts and the NULL reads come from Microsoft's page of
 * ID3D11DeviceContext::PSSetShaderResources (read 2026-10-10: "The method will hold a reference
 * to the interfaces passed in"): rt_nulldev_new gives the caller one reference, and each slot
 * that holds a view holds one more.  The overlap rule ("If an overlapping resource view is
 * already bound to an output slot, such as a rendertarget, then this API will fill the
 * destination shader resource slot with NULL") is the page's, and the case is Wine's
 * test_sample_attached_rtv.  The slot count 128 is the header's number: the page names
 * D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT and not its value.  The NULL-element unbinding, the
 * NULL-array stop, the range stop and a count of 0 doing nothing are the notes' rules.  None of
 * these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, PSSetShaderResources)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11ShaderResourceView *a = rt_nulldev_new(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView *b = rt_nulldev_new(device, &rt_nulldev_srv_class);
    ID3D11Texture2D *tex = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    ID3D11Texture2D *tex2 = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    ID3D11RenderTargetView *rtv = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    ID3D11ShaderResourceView *sv = rt_nulldev_new(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView *sv2 = rt_nulldev_new(device, &rt_nulldev_srv_class);
    NdView *v;
    /* sv is a view of tex; rtv is a render-target view of tex; sv2 is a view of tex2. */
    v = rt_com_state_of(sv, &rt_nulldev_srv_class);
    rt_com_addref(tex);
    v->resource = (ID3D11Resource *)tex;
    v = rt_com_state_of(rtv, &rt_nulldev_rtv_class);
    rt_com_addref(tex);
    v->resource = (ID3D11Resource *)tex;
    v = rt_com_state_of(sv2, &rt_nulldev_srv_class);
    rt_com_addref(tex2);
    v->resource = (ID3D11Resource *)tex2;
    ID3D11ShaderResourceView const *one[1] = { NULL }, *two[2] = { NULL, NULL }, *nine[9] = { NULL };

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->ps_srv[0] == NULL);
    T_TRUE(c->ps_srv[127] == NULL);
    T_TRUE(c->ps_srv[5] == NULL);
    T_TRUE(c->vs_cb[5] == NULL);
    T_EQ(rt_com_refs(a), 1);

    T_CASE("a Set binds the view and holds one reference to it");
    one[0] = a;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 5, 1, one);
    T_TRUE(c->ps_srv[5] == a);
    T_TRUE(c->ps_srv[4] == NULL);
    T_TRUE(c->ps_srv[6] == NULL);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a Set over an occupied slot releases the old view");
    one[0] = b;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 5, 1, one);
    T_TRUE(c->ps_srv[5] == b);
    T_EQ(rt_com_refs(a), 1);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("a NULL element unbinds its slot and releases the view");
    one[0] = NULL;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 5, 1, one);
    T_TRUE(c->ps_srv[5] == NULL);
    T_EQ(rt_com_refs(b), 1);

    T_CASE("NumViews above 1 fills consecutive slots");
    two[0] = a;
    two[1] = b;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 2, 2, two);
    T_TRUE(c->ps_srv[2] == a);
    T_TRUE(c->ps_srv[3] == b);
    T_TRUE(c->ps_srv[4] == NULL);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 2);

    T_CASE("slot 127 is the last slot");
    one[0] = b;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 127, 1, one);
    T_TRUE(c->ps_srv[127] == b);
    T_EQ(rt_com_refs(b), 3);

    T_CASE("a count of 0 with a NULL array does nothing");
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 2, 0, NULL);
    T_TRUE(c->ps_srv[2] == a);
    T_EQ(rt_com_refs(a), 2);
    T_EQ(rt_com_refs(b), 3);

    T_CASE("a view bound as a render target binds NULL");
    rt_nulldev_bind(&c->rtv[0], rtv);
    T_EQ(rt_com_refs(rtv), 2);
    one[0] = a;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 9, 1, one);
    T_TRUE(c->ps_srv[9] == a);
    T_EQ(rt_com_refs(a), 3);
    one[0] = sv;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 9, 1, one);
    T_TRUE(c->ps_srv[9] == NULL);
    T_EQ(rt_com_refs(sv), 1);
    T_EQ(rt_com_refs(a), 2);

    T_CASE("a view of a second texture binds as usual while the target is bound");
    one[0] = sv2;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 10, 1, one);
    T_TRUE(c->ps_srv[10] == sv2);
    T_EQ(rt_com_refs(sv2), 2);

    T_CASE("a view of the target binds again once the target is unbound");
    rt_nulldev_bind(&c->rtv[0], NULL);
    T_EQ(rt_com_refs(rtv), 1);
    one[0] = sv;
    COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 9, 1, one);
    T_TRUE(c->ps_srv[9] == sv);
    T_EQ(rt_com_refs(sv), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 120, 9, nine), "PSSetShaderResources for slots 120 to 128");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 0xffffffffu, 2, two), "PSSetShaderResources for slots 4294967295");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 200, 0, NULL), "PSSetShaderResources for slots 200 to 199");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, PSSetShaderResources, context, 0, 1, NULL), "PSSetShaderResources with a NULL array");
}
