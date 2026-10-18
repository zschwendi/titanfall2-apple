/* rt_nulldev_is_target and rt_nulldev_unbind_readers (rt/nulldev.h, "read and write";
 * runtime/nulldev/hazard.c): a subresource bound for writing is not also bound for reading.
 *
 * Where the expected values come from (all read 2026-10-10, none from hazard.c):
 *   - Microsoft's pages of PSSetShaderResources ("If an overlapping resource view is already
 *     bound to an output slot, such as a rendertarget, then this API will fill the destination
 *     shader resource slot with NULL") and OMSetRenderTargets (what is bound for reading:
 *     "those bind points will be set to NULL"); of OMSetRenderTargetsAndUnorderedAccessViews
 *     (views that share a subresource "therefore share the same resource": views of two
 *     resources never overlap); of D3D11_TEX2D_SRV (MipLevels -1 is every level from
 *     MostDetailedMip down).
 *   - Wine's conformance test d3d11.c, test_sample_attached_rtv: a view of mip 0 against a
 *     render target of mip 0 reads back NULL, a view of mip 1 stays; against a depth-stencil
 *     view, flags 0 unbind, READ_ONLY_DEPTH keeps a view of format R24_UNORM_X8_TYPELESS (46)
 *     or R32_FLOAT (41) and unbinds one of X24_TYPELESS_G8_UINT (47), READ_ONLY_STENCIL keeps
 *     the latter.
 *   - The reference counts: rt_nulldev_new gives one, a slot of the record holds one more
 *     (rt_nulldev_bind), and a slot bound to NULL gives its reference back.
 *   - That a record with no description stands for the whole resource is the header's rule. */
#include "rt/test.h"
#include "rt/nulldev.h"

static void *view_of(ID3D11Device *device, const RtComClass *cls, void *resource) {
    void *v = rt_nulldev_new(device, cls);
    NdView *r = rt_com_state_of(v, cls);
    if (resource) rt_com_addref(resource);
    r->resource = resource;
    return v;
}

static void *srv_mips(ID3D11Device *device, void *texture, UINT format, UINT first, UINT levels) {
    void *v = view_of(device, &rt_nulldev_srv_class, texture);
    NdView *r = rt_com_state_of(v, &rt_nulldev_srv_class);
    r->desc.srv.Format = format;
    r->desc.srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    r->desc.srv.Texture2D.MostDetailedMip = first;
    r->desc.srv.Texture2D.MipLevels = levels;
    return v;
}

static void *texture(ID3D11Device *device, UINT mips, UINT slices) {
    void *t = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    NdTexture *r = rt_com_state_of(t, &rt_nulldev_texture2d_class);
    r->dim = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    r->desc.d2.MipLevels = mips;
    r->desc.d2.ArraySize = slices;
    return t;
}

TEST(core, nulldev_hazard)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    Ctx *ctx = T_CTX();
    void *t1 = texture(device, 2, 1), *t2 = texture(device, 1, 1);

    T_CASE("views of two resources never overlap, and a view with no resource overlaps nothing");
    void *whole_rtv = view_of(device, &rt_nulldev_rtv_class, t1);
    void *whole_srv = view_of(device, &rt_nulldev_srv_class, t1);
    void *other_srv = view_of(device, &rt_nulldev_srv_class, t2);
    void *bare_srv = view_of(device, &rt_nulldev_srv_class, NULL);
    T_TRUE(!rt_nulldev_is_target(ctx, c, whole_srv));               /* nothing is bound for output yet */
    T_TRUE(!rt_nulldev_is_target(ctx, c, NULL));
    rt_nulldev_bind(&c->rtv[2], whole_rtv);
    T_TRUE(!rt_nulldev_is_target(ctx, c, other_srv));
    T_TRUE(!rt_nulldev_is_target(ctx, c, bare_srv));
    rt_nulldev_bind(&c->ps_srv[7], other_srv);
    rt_nulldev_bind(&c->ps_srv[8], bare_srv);
    rt_nulldev_unbind_readers(ctx, c, whole_rtv);
    T_TRUE(c->ps_srv[7] == other_srv);
    T_TRUE(c->ps_srv[8] == bare_srv);
    T_EQ(rt_com_refs(other_srv), 2);

    T_CASE("a view of a resource bound as a render target is a target, and binding the target unbinds it");
    T_TRUE(rt_nulldev_is_target(ctx, c, whole_srv));
    rt_nulldev_bind(&c->ps_srv[0], whole_srv);
    rt_nulldev_bind(&c->ps_srv[127], whole_srv);
    T_EQ(rt_com_refs(whole_srv), 3);
    rt_nulldev_unbind_readers(ctx, c, NULL);
    T_TRUE(c->ps_srv[0] == whole_srv);
    rt_nulldev_unbind_readers(ctx, c, whole_rtv);
    T_TRUE(c->ps_srv[0] == NULL);
    T_TRUE(c->ps_srv[127] == NULL);
    T_TRUE(c->ps_srv[7] == other_srv);
    T_EQ(rt_com_refs(whole_srv), 1);
    T_EQ(rt_com_refs(whole_rtv), 2);                                /* the record's slot 2 and the test */

    T_CASE("of a 2D texture, a view of mip 0 overlaps a target of mip 0 and a view of mip 1 does not");
    void *rtv0 = view_of(device, &rt_nulldev_rtv_class, t1);
    NdView *r = rt_com_state_of(rtv0, &rt_nulldev_rtv_class);
    r->desc.rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    r->desc.rtv.Texture2D.MipSlice = 0;
    void *srv0 = srv_mips(device, t1, 28, 0, 1), *srv1 = srv_mips(device, t1, 28, 1, 1);
    void *srv_all = srv_mips(device, t1, 28, 0, 0xffffffffu), *srv_from1 = srv_mips(device, t1, 28, 1, 0xffffffffu);
    rt_nulldev_bind(&c->rtv[2], NULL);
    rt_nulldev_bind(&c->rtv[0], rtv0);
    T_TRUE(rt_nulldev_is_target(ctx, c, srv0));
    T_TRUE(!rt_nulldev_is_target(ctx, c, srv1));
    T_TRUE(rt_nulldev_is_target(ctx, c, srv_all));
    T_TRUE(!rt_nulldev_is_target(ctx, c, srv_from1));
    T_TRUE(rt_nulldev_is_target(ctx, c, whole_srv));                /* no description: the whole texture */
    rt_nulldev_bind(&c->ps_srv[0], srv0);
    rt_nulldev_bind(&c->ps_srv[1], srv1);
    rt_nulldev_bind(&c->ps_srv[2], srv_all);
    rt_nulldev_unbind_readers(ctx, c, rtv0);
    T_TRUE(c->ps_srv[0] == NULL);
    T_TRUE(c->ps_srv[1] == srv1);
    T_TRUE(c->ps_srv[2] == NULL);
    T_EQ(rt_com_refs(srv0), 1);
    T_EQ(rt_com_refs(srv1), 2);

    T_CASE("a depth-stencil view unbinds a view of its texture unless it is read-only for what the view reads");
    void *depth = texture(device, 1, 1);
    void *dsv = view_of(device, &rt_nulldev_dsv_class, depth);
    NdView *d = rt_com_state_of(dsv, &rt_nulldev_dsv_class);
    d->desc.dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    void *reads_depth = srv_mips(device, depth, 46, 0, 1), *reads_stencil = srv_mips(device, depth, 47, 0, 1);
    void *reads_d32 = srv_mips(device, depth, 41, 0, 1);
    rt_nulldev_bind(&c->dsv, dsv);
    T_TRUE(rt_nulldev_is_target(ctx, c, reads_depth));              /* flags 0: both planes are written */
    T_TRUE(rt_nulldev_is_target(ctx, c, reads_stencil));
    T_TRUE(!rt_nulldev_is_target(ctx, c, srv1));                    /* another texture's view */
    d->desc.dsv.Flags = 1;                                          /* D3D11_DSV_READ_ONLY_DEPTH */
    T_TRUE(!rt_nulldev_is_target(ctx, c, reads_depth));
    T_TRUE(!rt_nulldev_is_target(ctx, c, reads_d32));
    T_TRUE(rt_nulldev_is_target(ctx, c, reads_stencil));
    rt_nulldev_bind(&c->ps_srv[4], reads_depth);
    rt_nulldev_bind(&c->ps_srv[5], reads_stencil);
    rt_nulldev_unbind_readers(ctx, c, dsv);
    T_TRUE(c->ps_srv[4] == reads_depth);
    T_TRUE(c->ps_srv[5] == NULL);
    T_EQ(rt_com_refs(reads_stencil), 1);
    d->desc.dsv.Flags = 2;                                          /* D3D11_DSV_READ_ONLY_STENCIL */
    T_TRUE(!rt_nulldev_is_target(ctx, c, reads_stencil));
    T_TRUE(rt_nulldev_is_target(ctx, c, reads_depth));
    rt_nulldev_unbind_readers(ctx, c, dsv);
    T_TRUE(c->ps_srv[4] == NULL);

    /* What no source here settles stops: a texture array, and a format whose plane is not known. */
    void *array = texture(device, 1, 6);
    void *array_rtv = view_of(device, &rt_nulldev_rtv_class, array);
    r = rt_com_state_of(array_rtv, &rt_nulldev_rtv_class);
    r->desc.rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    void *array_srv = srv_mips(device, array, 28, 0, 1);
    rt_nulldev_bind(&c->rtv[1], array_rtv);
    T_TRAPS(rt_nulldev_is_target(ctx, c, array_srv), "of one D3D11Texture2D share a subresource");
    rt_nulldev_bind(&c->rtv[1], NULL);
    void *reads_other = srv_mips(device, depth, 28, 0, 1);
    T_TRAPS(rt_nulldev_is_target(ctx, c, reads_other), "of format 28 reads what a depth-stencil view with flags 0x2 writes");
}
