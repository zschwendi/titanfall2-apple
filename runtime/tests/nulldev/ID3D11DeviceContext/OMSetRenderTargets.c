/* Expected values.  The reference counts come from Microsoft's page of
 * ID3D11DeviceContext::OMSetRenderTargets (read 2026-10-10: "The method will hold a reference to
 * the interfaces passed in"): rt_nulldev_new gives the caller one reference, and each slot that
 * holds a view holds one more.  "Any render targets not defined by this call are set to NULL" gives
 * the NULL slots after NumViews.  "If any subresources are also currently bound for reading ...
 * those bind points will be set to NULL" gives the reader cases: the views of a texture are made in
 * the test by setting the view's resource, as the notes say.  The stops (9 views, a NULL array) are
 * the notes' rules.  None of these values comes from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, OMSetRenderTargets)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    ID3D11RenderTargetView *r1 = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    ID3D11RenderTargetView *r2 = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    ID3D11DepthStencilView *d1 = rt_nulldev_new(device, &rt_nulldev_dsv_class);
    ID3D11RenderTargetView const *one[1] = { NULL }, *two[2] = { NULL, NULL }, *eight[8] = { NULL };
    ID3D11RenderTargetView const *nine[9] = { NULL };

    T_CASE("a slot never set reads NULL");
    T_TRUE(c->rtv[0] == NULL);
    T_TRUE(c->rtv[7] == NULL);
    T_TRUE(c->dsv == NULL);
    T_EQ(rt_com_refs(r1), 1);

    T_CASE("a Set binds the view and holds one reference to it");
    one[0] = r1;
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 1, one, NULL);
    T_TRUE(c->rtv[0] == r1);
    T_TRUE(c->rtv[1] == NULL);
    T_EQ(rt_com_refs(r1), 2);

    T_CASE("a Set over an occupied slot releases the old view");
    one[0] = r2;
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 1, one, NULL);
    T_TRUE(c->rtv[0] == r2);
    T_EQ(rt_com_refs(r1), 1);
    T_EQ(rt_com_refs(r2), 2);

    T_CASE("a Set of one view after two leaves slot 1 NULL and released");
    two[0] = r1;
    two[1] = r2;
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 2, two, NULL);
    T_TRUE(c->rtv[0] == r1);
    T_TRUE(c->rtv[1] == r2);
    T_TRUE(c->rtv[2] == NULL);
    T_EQ(rt_com_refs(r2), 2);
    one[0] = r1;
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 1, one, NULL);
    T_TRUE(c->rtv[0] == r1);
    T_TRUE(c->rtv[1] == NULL);
    T_EQ(rt_com_refs(r1), 2);
    T_EQ(rt_com_refs(r2), 1);

    T_CASE("NumViews of 8 fills every slot");
    for (int i = 0; i < 8; i++)
        eight[i] = r2;
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 8, eight, NULL);
    T_TRUE(c->rtv[0] == r2);
    T_TRUE(c->rtv[7] == r2);
    T_EQ(rt_com_refs(r1), 1);
    T_EQ(rt_com_refs(r2), 9);
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 0, NULL, NULL);
    T_TRUE(c->rtv[0] == NULL);
    T_TRUE(c->rtv[7] == NULL);
    T_EQ(rt_com_refs(r2), 1);

    T_CASE("a depth-stencil Set binds the view and holds one reference to it");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 0, NULL, d1);
    T_TRUE(c->dsv == d1);
    T_EQ(rt_com_refs(d1), 2);

    T_CASE("a NULL depth-stencil view unbinds it and releases it");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 0, NULL, NULL);
    T_TRUE(c->dsv == NULL);
    T_EQ(rt_com_refs(d1), 1);

    /* The second rule: a view of a texture that is also bound for reading loses that binding. */
    ID3D11Texture2D *t1 = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    ID3D11Texture2D *t2 = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    ID3D11RenderTargetView *r3 = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    ID3D11DepthStencilView *d2 = rt_nulldev_new(device, &rt_nulldev_dsv_class);
    ID3D11ShaderResourceView *srv1 = rt_nulldev_new(device, &rt_nulldev_srv_class);
    ID3D11ShaderResourceView *srv2 = rt_nulldev_new(device, &rt_nulldev_srv_class);
    NdView *v;
    v = rt_com_state_of(r3, &rt_nulldev_rtv_class);
    rt_com_addref(t1);
    v->resource = (ID3D11Resource *)t1;
    v = rt_com_state_of(d2, &rt_nulldev_dsv_class);
    rt_com_addref(t2);
    v->resource = (ID3D11Resource *)t2;
    v = rt_com_state_of(srv1, &rt_nulldev_srv_class);
    rt_com_addref(t1);
    v->resource = (ID3D11Resource *)t1;
    v = rt_com_state_of(srv2, &rt_nulldev_srv_class);
    rt_com_addref(t2);
    v->resource = (ID3D11Resource *)t2;
    rt_nulldev_bind(&c->ps_srv[3], srv1);
    rt_nulldev_bind(&c->ps_srv[4], srv2);
    T_EQ(rt_com_refs(srv1), 2);
    T_EQ(rt_com_refs(srv2), 2);

    T_CASE("a Set binds a view of a texture being read unbinds the reader");
    ID3D11RenderTargetView const *w[1] = { r3 };
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 1, w, NULL);
    T_TRUE(c->rtv[0] == r3);
    T_TRUE(c->ps_srv[3] == NULL);
    T_EQ(rt_com_refs(srv1), 1);
    T_TRUE(c->ps_srv[4] == srv2);
    T_EQ(rt_com_refs(srv2), 2);

    T_CASE("a depth-stencil Set unbinds a reader of its texture");
    COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 0, NULL, d2);
    T_TRUE(c->dsv == d2);
    T_TRUE(c->ps_srv[4] == NULL);
    T_EQ(rt_com_refs(srv2), 1);
    T_EQ(rt_com_refs(d2), 2);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 9, nine, NULL), "OMSetRenderTargets with");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, OMSetRenderTargets, context, 1, NULL, NULL), "OMSetRenderTargets with a NULL array");
}
