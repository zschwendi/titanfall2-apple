/* One subresource is not bound for reading and for writing at once: what the context's
 * output bindings and its shader-resource bindings do to each other (rt/nulldev.h, the two
 * functions under "read and write").  Unit d3d-rows-fix, for E3 of
 * docs/reviews/d3d-contract-check.md.
 *
 * SOURCES, all read 2026-10-10.
 *   Microsoft, ID3D11DeviceContext::PSSetShaderResources: "If an overlapping resource view is
 *     already bound to an output slot, such as a rendertarget, then this API will fill the
 *     destination shader resource slot with NULL."
 *   Microsoft, ID3D11DeviceContext::OMSetRenderTargets: "If any subresources are also
 *     currently bound for reading in a different stage or writing (perhaps in a different part
 *     of the pipeline), those bind points will be set to NULL".
 *   Microsoft, ...::OMSetRenderTargetsAndUnorderedAccessViews: two views conflict "if they share
 *     a subresource (and therefore share the same resource)".
 *   Microsoft, D3D11_TEX2D_SRV: MipLevels "-1 to indicate all the mipmap levels from
 *     MostDetailedMip on down to least detailed".
 *   Wine's conformance test dlls/d3d11/tests/d3d11.c, test_sample_attached_rtv (what it asserts,
 *     in this file's words): a view of mip 0 of a 2D texture set while a render-target view of
 *     mip 0 is bound reads back NULL, and one that was bound reads back NULL once that target is
 *     set; a view of mip 1 stays.  With a depth-stencil view of the same texture: flags 0 unbind
 *     the view; D3D11_DSV_READ_ONLY_DEPTH leaves a view of format R24_UNORM_X8_TYPELESS or
 *     R32_FLOAT bound and unbinds one of format X24_TYPELESS_G8_UINT; READ_ONLY_STENCIL leaves
 *     the latter bound.
 *
 * WHAT IS DECIDED HERE, and nothing more: views of different resources never share; a view
 * whose description nothing filled (ViewDimension 0, which no creator writes: an object made
 * by rt_nulldev_new) stands for the whole resource; two TEXTURE2D-dimension views of a 2D
 * texture that is no array share when their mip ranges meet; a read-only depth-stencil view
 * and a shader-resource view of the three formats above follow the test.  Every other pair of
 * views of ONE resource is a stop that names the two views: arrays, cubes, 1D and 3D textures,
 * buffers and multisampled views have no source here yet. */
#include "rt/nulldev.h"

enum { COVERS_ALL, COVERS_MIPS, COVERS_UNKNOWN };

static const NdView *view_record(void *view, const RtComClass **cls) {
    *cls = rt_com_class_of(view);
    if (*cls != &rt_nulldev_srv_class && *cls != &rt_nulldev_uav_class && *cls != &rt_nulldev_rtv_class &&
        *cls != &rt_nulldev_dsv_class)
        rt_fatal("rt/nulldev.h, read and write: %p is %s, not a view", view, *cls ? (*cls)->name : "no live object");
    return rt_com_state_of(view, *cls);
}

/* ViewDimension is the second UINT of all four descriptions (rt/win/d3d11view.h asserts it). */
static UINT dimension(const NdView *v) { return v->desc.srv.ViewDimension; }

static int covers(const NdView *v, const RtComClass *cls, UINT *first, UINT *end) {
    UINT dim = dimension(v);
    if (dim == 0) return COVERS_ALL;
    const RtComClass *rc = rt_com_class_of(v->resource);
    if (rc != &rt_nulldev_texture2d_class) return COVERS_UNKNOWN;
    if (((const NdTexture *)rt_com_state_of(v->resource, rc))->desc.d2.ArraySize != 1) return COVERS_UNKNOWN;
    if (cls == &rt_nulldev_srv_class) {
        if (dim != D3D11_SRV_DIMENSION_TEXTURE2D) return COVERS_UNKNOWN;
        *first = v->desc.srv.Texture2D.MostDetailedMip;
        UINT n = v->desc.srv.Texture2D.MipLevels;
        *end = n == 0xffffffffu || n > 0xffffffffu - *first ? 0xffffffffu : *first + n;
        return COVERS_MIPS;
    }
    if (cls == &rt_nulldev_rtv_class && dim == D3D11_RTV_DIMENSION_TEXTURE2D) *first = v->desc.rtv.Texture2D.MipSlice;
    else if (cls == &rt_nulldev_dsv_class && dim == D3D11_DSV_DIMENSION_TEXTURE2D) *first = v->desc.dsv.Texture2D.MipSlice;
    else if (cls == &rt_nulldev_uav_class && dim == D3D11_UAV_DIMENSION_TEXTURE2D) *first = v->desc.uav.Texture2D.MipSlice;
    else return COVERS_UNKNOWN;
    *end = *first + 1;
    return COVERS_MIPS;
}

/* Do the two views (either may be NULL) share a subresource? */
static bool share(Ctx *ctx, void *a, void *b) {
    if (!a || !b) return false;
    const RtComClass *ca, *cb;
    const NdView *va = view_record(a, &ca), *vb = view_record(b, &cb);
    if (!va->resource || !vb->resource || rt_com_serial(va->resource) != rt_com_serial(vb->resource)) return false;
    UINT fa = 0, ea = 0, fb = 0, eb = 0;
    int ka = covers(va, ca, &fa, &ea), kb = covers(vb, cb, &fb, &eb);
    if (ka == COVERS_ALL || kb == COVERS_ALL) return true;
    if (ka == COVERS_UNKNOWN || kb == COVERS_UNKNOWN)
        RT_UNIMPLEMENTED(ctx, "whether a %s (view dimension %u) and a %s (view dimension %u) of one %s share a subresource: "
                         "only TEXTURE2D views of a 2D texture that is no array are modelled (runtime/nulldev/hazard.c)",
                         ca->name, dimension(va), cb->name, dimension(vb), rt_com_class_of(va->resource)->name);
    return fa < eb && fb < ea;
}

/* Is what `view` covers written through the depth-stencil view `dsv`? */
static bool written_by_depth(Ctx *ctx, void *view, void *dsv) {
    if (!share(ctx, view, dsv)) return false;
    const RtComClass *cls, *unused;
    const NdView *v = view_record(view, &cls);
    UINT flags = view_record(dsv, &unused)->desc.dsv.Flags, format = v->desc.srv.Format, reads = 0;
    if (!flags) return true;
    if (cls == &rt_nulldev_srv_class) {
        if (format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS || format == DXGI_FORMAT_R32_FLOAT) reads = D3D11_DSV_READ_ONLY_DEPTH;
        if (format == DXGI_FORMAT_X24_TYPELESS_G8_UINT) reads = D3D11_DSV_READ_ONLY_STENCIL;
    }
    if (!reads)
        RT_UNIMPLEMENTED(ctx, "whether a %s of format %u reads what a depth-stencil view with flags %#x writes "
                         "(runtime/nulldev/hazard.c has three formats)", cls->name, format, flags);
    return !(flags & reads);
}

bool rt_nulldev_is_target(Ctx *ctx, const NdContext *c, void *view) {
    if (!view) return false;
    for (unsigned i = 0; i < ND_RT_SLOTS; i++)
        if (share(ctx, view, c->rtv[i])) return true;
    return c->dsv && written_by_depth(ctx, view, c->dsv);
}

void rt_nulldev_unbind_readers(Ctx *ctx, NdContext *c, void *target) {
    if (!target) return;
    bool depth = rt_com_class_of(target) == &rt_nulldev_dsv_class;
    for (unsigned i = 0; i < ND_SRV_SLOTS; i++)
        if (c->ps_srv[i] && (depth ? written_by_depth(ctx, c->ps_srv[i], target) : share(ctx, c->ps_srv[i], target)))
            rt_nulldev_bind(&c->ps_srv[i], NULL);
}
