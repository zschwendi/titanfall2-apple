/* The common parts of the creators that keep no memory: state objects (shared when equal),
 * views, shaders, the input layout's elements, the swap chain's back buffer (rt/nulldev.h has
 * what each does; docs/handoff/d3d-resources.md, section 3, the sources). */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/nulldev.h"

/* ---- state objects ---------------------------------------------------------------------------------
 * Microsoft, read 2026-10-10 (by a subagent that returned the text): CreateSamplerState,
 * CreateRasterizerState, CreateDepthStencilState: "4096 unique ... state objects can be created
 * on a device at a time.  If an application attempts to create a ...-state interface with the
 * same state as an existing interface, the same interface will be returned and the total number
 * of unique ... state objects will stay the same."  CreateBlendState: "For each object created,
 * the runtime checks to see if a previous object has the same state.  If such a previous object
 * exists, the runtime will return a pointer to previous instance instead of creating a
 * duplicate object."
 * Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, E5): equal means equal after Windows
 * has normalised the description (state_normalise below), the normalised description is what is
 * kept, and the second create returns the same object with one more reference.
 * From memory: Direct3D rejects an enumerator outside its enumeration (the ranges below are the
 * enumerations of rt/win/d3d11.h; they are applied to the normalised description, so only to
 * fields Windows uses).
 *
 * WHAT WINDOWS USES OF A BLEND DESCRIPTION (unit d3d-fix-2; the game leaves stray numbers in the
 * rest, and the real device took all 27 recorded descriptions: docs/findings/40-d3d-call-trace.md,
 * row N; docs/trace/d3d-creators.md):
 *   Microsoft, D3D11_BLEND_DESC (read 2026-10-10), IndependentBlendEnable: "If set to FALSE, only
 *   the RenderTarget[0] members are used; RenderTarget[1..7] are ignored."
 *   Wine, dlls/d3d11/tests/d3d11.c, test_create_blend_state (read 2026-10-10): a description that
 *   is all zeros but for RenderTarget[0].RenderTargetWriteMask = ALL (so BlendEnable FALSE and
 *   every factor and operation 0, which is no enumerator) is created with S_OK, and GetDesc reads
 *   back, for each of the 8 targets: BlendEnable FALSE, SrcBlend ONE, DestBlend ZERO, BlendOp ADD,
 *   SrcBlendAlpha ONE, DestBlendAlpha ZERO, BlendOpAlpha ADD, the write mask ALL.  The same
 *   values are the documented defaults (Microsoft, D3D11_RENDER_TARGET_BLEND_DESC, Remarks).
 * So: a target whose BlendEnable is FALSE has its four factors and two operations replaced by
 * those defaults and never range-checked; its write mask is kept.  The test shows it for the
 * value 0; that Windows does the same for any other stray number is this unit's reading (the
 * recording's device did).
 *   Microsoft, D3D11_RENDER_TARGET_BLEND_DESC (read 2026-10-10), SrcBlendAlpha and
 *   DestBlendAlpha: "Blend options that end in _COLOR are not allowed."  A stop here.
 * From memory: a write mask above D3D11_COLOR_WRITE_ENABLE_ALL is rejected. */

static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static const RtComClass *const state_class[4] = { &rt_nulldev_blend_class, &rt_nulldev_depthstencil_class,
                                                  &rt_nulldev_rasterizer_class, &rt_nulldev_sampler_class };
static const uint32_t state_bytes[4] = { sizeof(D3D11_BLEND_DESC), sizeof(D3D11_DEPTH_STENCIL_DESC),
                                         sizeof(D3D11_RASTERIZER_DESC), sizeof(D3D11_SAMPLER_DESC) };

static bool in(UINT v, UINT lo, UINT hi) { return v >= lo && v <= hi; }

static bool stencil_ok(const D3D11_DEPTH_STENCILOP_DESC *f) {
    return in(f->StencilFailOp, 1, 8) && in(f->StencilDepthFailOp, 1, 8) && in(f->StencilPassOp, 1, 8) && in(f->StencilFunc, 1, 8);
}

/* D3D11_BLEND is 1 to 11 and 14 to 19 (rt/win/d3d11.h: 12 and 13 are no enumerator); the six
 * that end in _COLOR are not allowed for alpha (above). */
static bool blend_factor(UINT v) { return in(v, 1, 11) || in(v, 14, 19); }
static bool blend_factor_alpha(UINT v) {
    return blend_factor(v) && v != D3D11_BLEND_SRC_COLOR && v != D3D11_BLEND_INV_SRC_COLOR && v != D3D11_BLEND_DEST_COLOR &&
           v != D3D11_BLEND_INV_DEST_COLOR && v != D3D11_BLEND_SRC1_COLOR && v != D3D11_BLEND_INV_SRC1_COLOR;
}

/* What is wrong with the NORMALISED description `desc` of kind k, or NULL.  For a blend
 * description *target is the render target the problem is in. */
static const char *state_problem(unsigned k, const void *desc, unsigned *target) {
    if (k == 0) {
        const D3D11_BLEND_DESC *d = desc;
        for (unsigned i = 0; i < 8; i++) {
            const D3D11_RENDER_TARGET_BLEND_DESC *t = &d->RenderTarget[i];
            *target = i;
            if (t->BlendEnable) {       /* a target that does not blend holds the defaults: nothing to check */
                if (!blend_factor(t->SrcBlend) || !blend_factor(t->DestBlend)) return "a blend factor that is not one of D3D11_BLEND";
                if (!blend_factor_alpha(t->SrcBlendAlpha) || !blend_factor_alpha(t->DestBlendAlpha))
                    return "an alpha blend factor that is not one of D3D11_BLEND or ends in _COLOR";
                if (!in(t->BlendOp, 1, 5) || !in(t->BlendOpAlpha, 1, 5)) return "a blend operation that is not one of D3D11_BLEND_OP";
            }
            if (t->RenderTargetWriteMask > D3D11_COLOR_WRITE_ENABLE_ALL) return "a write mask above D3D11_COLOR_WRITE_ENABLE_ALL";
        }
    } else if (k == 1) {
        const D3D11_DEPTH_STENCIL_DESC *d = desc;
        if (d->DepthWriteMask > 1) return "a depth write mask that is not one of D3D11_DEPTH_WRITE_MASK";
        if (!in(d->DepthFunc, 1, 8)) return "a depth function that is not one of D3D11_COMPARISON_FUNC";
        if (!stencil_ok(&d->FrontFace) || !stencil_ok(&d->BackFace)) return "a stencil operation or function outside its enumeration";
    } else if (k == 2) {
        const D3D11_RASTERIZER_DESC *d = desc;
        if (!in(d->FillMode, 2, 3)) return "a fill mode that is not one of D3D11_FILL_MODE";
        if (!in(d->CullMode, 1, 3)) return "a cull mode that is not one of D3D11_CULL_MODE";
    } else {
        const D3D11_SAMPLER_DESC *d = desc;
        UINT f = d->Filter & ~0x80u;            /* 0x80: the comparison form of a filter */
        if (f != 0x55 && (f & ~0x15u)) return "a filter that is not one of D3D11_FILTER";
        if (!in(d->AddressU, 1, 5) || !in(d->AddressV, 1, 5) || !in(d->AddressW, 1, 5))
            return "an address mode that is not one of D3D11_TEXTURE_ADDRESS_MODE";
        if (!in(d->ComparisonFunc, 1, 8)) return "a comparison filter whose function is not one of D3D11_COMPARISON_FUNC";
        if (d->MaxAnisotropy > 16) return "a maximum anisotropy above 16";
    }
    return NULL;
}

/* What Windows makes of a description before it compares and keeps it.  Source: Wine's d3d11
 * conformance tests, as read by the contract's check (docs/reviews/d3d-contract-check.md, E5:
 * test_create_sampler_state, test_create_blend_state, test_create_depthstencil_state):
 *   blend          with IndependentBlendEnable FALSE, render targets 1 to 7 are copies of 0; a
 *                  target whose BlendEnable is FALSE holds ONE, ZERO, ADD, ONE, ZERO, ADD
 *                  whatever was passed, and its own write mask (the head of this section)
 *   depth-stencil  with DepthEnable FALSE: DepthWriteMask ALL (1), DepthFunc LESS (2); with
 *                  StencilEnable FALSE: both masks 0xff, both faces KEEP, KEEP, KEEP, ALWAYS
 *   sampler        MaxAnisotropy 0 unless the filter is anisotropic; ComparisonFunc NEVER (1)
 *                  unless it is a comparison filter; BorderColor zeros unless an address mode
 *                  is BORDER (4).  (The tests show one linear, WRAP sampler; that the rule is
 *                  "fields the filter or addressing does not use" is the check's reading.)
 *   rasterizer     nothing
 * `out` is zeroed first and filled field by field, so padding never takes part. */
static void state_normalise(unsigned k, const void *desc, void *out) {
    memset(out, 0, state_bytes[k]);
    if (k == 0) {
        const D3D11_BLEND_DESC *d = desc;
        D3D11_BLEND_DESC *o = out;
        o->AlphaToCoverageEnable = d->AlphaToCoverageEnable;
        o->IndependentBlendEnable = d->IndependentBlendEnable;
        for (unsigned i = 0; i < 8; i++) {
            const D3D11_RENDER_TARGET_BLEND_DESC *t = &d->RenderTarget[d->IndependentBlendEnable ? i : 0];
            static const D3D11_RENDER_TARGET_BLEND_DESC off = { FALSE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD,
                                                                D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, 0 };
            const D3D11_RENDER_TARGET_BLEND_DESC *f = t->BlendEnable ? t : &off;
            o->RenderTarget[i].BlendEnable = t->BlendEnable;
            o->RenderTarget[i].SrcBlend = f->SrcBlend;
            o->RenderTarget[i].DestBlend = f->DestBlend;
            o->RenderTarget[i].BlendOp = f->BlendOp;
            o->RenderTarget[i].SrcBlendAlpha = f->SrcBlendAlpha;
            o->RenderTarget[i].DestBlendAlpha = f->DestBlendAlpha;
            o->RenderTarget[i].BlendOpAlpha = f->BlendOpAlpha;
            o->RenderTarget[i].RenderTargetWriteMask = t->RenderTargetWriteMask;
        }
    } else if (k == 1) {
        const D3D11_DEPTH_STENCIL_DESC *d = desc;
        D3D11_DEPTH_STENCIL_DESC *o = out;
        static const D3D11_DEPTH_STENCILOP_DESC keep = { 1, 1, 1, 8 };
        o->DepthEnable = d->DepthEnable;
        o->DepthWriteMask = d->DepthEnable ? d->DepthWriteMask : 1;
        o->DepthFunc = d->DepthEnable ? d->DepthFunc : 2;
        o->StencilEnable = d->StencilEnable;
        o->StencilReadMask = d->StencilEnable ? d->StencilReadMask : 0xff;
        o->StencilWriteMask = d->StencilEnable ? d->StencilWriteMask : 0xff;
        const D3D11_DEPTH_STENCILOP_DESC *f = d->StencilEnable ? &d->FrontFace : &keep, *b = d->StencilEnable ? &d->BackFace : &keep;
        o->FrontFace.StencilFailOp = f->StencilFailOp; o->FrontFace.StencilDepthFailOp = f->StencilDepthFailOp;
        o->FrontFace.StencilPassOp = f->StencilPassOp; o->FrontFace.StencilFunc = f->StencilFunc;
        o->BackFace.StencilFailOp = b->StencilFailOp; o->BackFace.StencilDepthFailOp = b->StencilDepthFailOp;
        o->BackFace.StencilPassOp = b->StencilPassOp; o->BackFace.StencilFunc = b->StencilFunc;
    } else if (k == 2) {
        memcpy(out, desc, state_bytes[k]);      /* 4-byte fields only: no padding */
    } else {
        D3D11_SAMPLER_DESC *o = out;
        memcpy(o, desc, state_bytes[k]);        /* 4-byte fields only: no padding */
        if ((o->Filter & ~0x80u) != 0x55) o->MaxAnisotropy = 0;
        if (!(o->Filter & 0x80u)) o->ComparisonFunc = 1;
        if (o->AddressU != 4 && o->AddressV != 4 && o->AddressW != 4) memset(o->BorderColor, 0, sizeof o->BorderColor);
    }
}

static unsigned state_kind(const RtComClass *cls) {
    for (unsigned k = 0; k < 4; k++) if (state_class[k] == cls) return k;
    rt_fatal("rt_nulldev: class %s is not a state class", cls ? cls->name : "(null)");
}

HRESULT rt_nulldev_state_create(Ctx *ctx, ID3D11Device *device, const RtComClass *cls, const void *desc, void **out) {
    unsigned k = state_kind(cls);
    NdDevice *dev = rt_com_state(ctx, device, &rt_nulldev_device_class);
    if (!desc || !out) RT_UNIMPLEMENTED(ctx, "a state creator with a NULL description or a NULL out-pointer (%p, %p)", desc, (void *)out);
    union { D3D11_BLEND_DESC b; D3D11_DEPTH_STENCIL_DESC d; D3D11_RASTERIZER_DESC r; D3D11_SAMPLER_DESC s; } norm;
    state_normalise(k, desc, &norm);
    unsigned target = 0;
    const char *why = state_problem(k, &norm, &target);
    if (why && k == 0) {                /* the target as the game passed it: its eight words */
        const D3D11_BLEND_DESC *d = desc;
        const D3D11_RENDER_TARGET_BLEND_DESC *t = &d->RenderTarget[d->IndependentBlendEnable ? target : 0];
        RT_UNIMPLEMENTED(ctx, "a %s description with %s in render target %u (IndependentBlendEnable %u; BlendEnable %u, colour %u %u "
                         "operation %u, alpha %u %u operation %u, write mask 0x%x)", cls->name, why, target,
                         (unsigned)d->IndependentBlendEnable, (unsigned)t->BlendEnable, (unsigned)t->SrcBlend, (unsigned)t->DestBlend,
                         (unsigned)t->BlendOp, (unsigned)t->SrcBlendAlpha, (unsigned)t->DestBlendAlpha, (unsigned)t->BlendOpAlpha,
                         (unsigned)t->RenderTargetWriteMask);
    }
    if (why) {
        char text[13 * 9 + 1];
        const uint32_t *w = desc;
        size_t n = 0;
        for (unsigned i = 0; i < 13 && i < state_bytes[k] / 4; i++) n += (size_t)snprintf(text + n, sizeof text - n, " %08x", (unsigned)w[i]);
        RT_UNIMPLEMENTED(ctx, "a %s description with %s; its words:%s", cls->name, why, text);
    }
    /* The list holds no reference, so an object in it may be one whose last Release is in
     * flight on another thread: its count is 0, its destroy (which takes this lock to unlink it)
     * has not finished, and its faces still say alive.  Such an object is never handed out
     * (rt_com_addref_live refuses it; docs/reviews/d3d-code-review.md, G4): the search goes on,
     * and a new object is made, which may stand in the list beside the dying one for a moment. */
    pthread_mutex_lock(&state_lock);
    void *found = dev->states[k];
    while (found) {
        NdStateObject *s = rt_com_state_of(found, cls);
        if (!memcmp(&s->desc, &norm, state_bytes[k]) && rt_com_addref_live(found)) break;
        found = s->next;
    }
    bool full = !found && dev->nstates[k] >= 4096;
    if (!found && !full) {
        found = rt_nulldev_new(device, cls);
        NdStateObject *s = rt_com_state_of(found, cls);
        memcpy(&s->desc, &norm, state_bytes[k]);
        s->next = dev->states[k];
        dev->states[k] = found;
        dev->nstates[k]++;
    }
    pthread_mutex_unlock(&state_lock);
    if (!found) RT_UNIMPLEMENTED(ctx, "the 4097th live %s of one device", cls->name);
    *out = found;
    return S_OK;
}

/* The destroy function of the four state classes calls this before it lets go of the device. */
void rt_nulldev_state_unlink(void *state, const RtComClass *cls) {
    unsigned k = state_kind(cls);
    NdStateObject *s = state;
    NdDevice *dev = rt_com_state_of(s->child.device, &rt_nulldev_device_class);
    pthread_mutex_lock(&state_lock);
    void **link = &dev->states[k];
    while (*link && rt_com_state_of(*link, cls) != state) link = &((NdStateObject *)rt_com_state_of(*link, cls))->next;
    if (*link) { *link = s->next; dev->nstates[k]--; }
    pthread_mutex_unlock(&state_lock);
}

/* ---- views -----------------------------------------------------------------------------------------
 * Microsoft, read 2026-10-10 (by a subagent): CreateShaderResourceView, pDesc: "Set this
 * parameter to NULL to create a view that accesses the entire resource (using the format the
 * resource was created with)"; CreateRenderTargetView: "... a view that accesses all of the
 * subresources in mipmap level 0"; CreateDepthStencilView: "... mipmap level 0 of the entire
 * resource (using the format the resource was created with)"; each out-pointer: "Set this
 * parameter to NULL to validate the other input parameters (the method will return S_FALSE if
 * the other input parameters pass validation)".
 *
 * THE DESCRIPTION A VIEW KEEPS is the one Windows reads back from it (unit d3d-fix-2;
 * docs/reviews/d3d-contract-check-2.md, F1, which read Wine's dlls/d3d11/tests/d3d11.c:
 * test_create_rendertarget_view, test_create_shader_resource_view,
 * test_create_depthstencil_view), not the bytes passed:
 *   a given description   DXGI_FORMAT_UNKNOWN on a texture becomes the texture's format; a
 *                         count of 0xffffffff (MipLevels, ArraySize, WSize) becomes "all the
 *                         rest": the resource's count less the first (Wine: MipLevels ~0u from
 *                         mip 3 of 10 reads back 7; ArraySize ~0u from slice 1 of 4 reads 3)
 *   a NULL description    the texture's format; a shader-resource view takes every mip level,
 *                         the others level 0; the dimension follows the texture: 2D, or 2D
 *                         array of all slices when ArraySize is above 1 (Wine: 10 mips gives
 *                         TEXTURE2D, MipSlice 0; 4 slices gives TEXTURE2DARRAY, first 0, size
 *                         4); a shader-resource view of a texture with the cube flag is
 *                         TEXTURECUBE under 12 slices and TEXTURECUBEARRAY of ArraySize / 6
 *                         cubes from 12 on (Wine: 6, 9, 11 slices; 12 to 14 give 2, 18 gives 3)
 * Not in the tests F1 read, so a stop when the description is NULL: a buffer, a multisampled
 * texture, a 1D array, a 3D texture for anything but a shader-resource view.  From memory:
 * which view dimension goes with which resource (the table below); a plain 1D texture and a
 * 3D texture's shader-resource view with a NULL description (as the 2D case); that WSize ~0u
 * counts the depth of the view's own mip level; NumCubes ~0u (a stop: no test read shows it). */

/* bit d of the mask: view dimension d may be made on this kind of resource (kind: 1 buffer, 2 to 4 textures 1D to 3D) */
static const uint16_t view_dims[4][5] = {
    /* SRV */ { 0, 1u << 1 | 1u << 11, 1u << 2 | 1u << 3, 1u << 4 | 1u << 5 | 1u << 6 | 1u << 7 | 1u << 9 | 1u << 10, 1u << 8 },
    /* UAV */ { 0, 1u << 1, 1u << 2 | 1u << 3, 1u << 4 | 1u << 5, 1u << 8 },
    /* RTV */ { 0, 1u << 1, 1u << 2 | 1u << 3, 1u << 4 | 1u << 5 | 1u << 6 | 1u << 7, 1u << 8 },
    /* DSV */ { 0, 0, 1u << 1 | 1u << 2, 1u << 3 | 1u << 4 | 1u << 5 | 1u << 6, 0 },
};
static const char *const kind_name[5] = { "", "buffer", "texture 1D", "texture 2D", "texture 3D" };

typedef union ViewDesc {
    D3D11_SHADER_RESOURCE_VIEW_DESC srv; D3D11_UNORDERED_ACCESS_VIEW_DESC uav; D3D11_RENDER_TARGET_VIEW_DESC rtv;
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
} ViewDesc;

/* What a view needs to know of its resource. */
typedef struct ViewOf { unsigned kind; UINT format, mips, array, depth, samples, bind, usage; bool cube; } ViewOf;

/* "All the rest": a count of ~0u stands for the total less the first. */
static UINT rest(UINT count, UINT total, UINT first) { return count != ~0u ? count : total > first ? total - first : 0; }
static UINT level_depth(UINT depth, UINT level) { return level < 32 && depth >> level ? depth >> level : 1; }

/* A given description `d` of view kind v (0 SRV, 1 UAV, 2 RTV, 3 DSV) as Windows keeps it. */
static void view_normalise(Ctx *ctx, const RtComClass *cls, unsigned v, const ViewOf *r, ViewDesc *d) {
    if (r->kind > 1 && d->srv.Format == DXGI_FORMAT_UNKNOWN) d->srv.Format = r->format;
    UINT dim = d->srv.ViewDimension;
    if (v == 0) {
        if (dim == D3D11_SRV_DIMENSION_TEXTURE1D || dim == D3D11_SRV_DIMENSION_TEXTURE2D || dim == D3D11_SRV_DIMENSION_TEXTURE3D ||
            dim == D3D11_SRV_DIMENSION_TEXTURECUBE || dim == D3D11_SRV_DIMENSION_TEXTURE1DARRAY ||
            dim == D3D11_SRV_DIMENSION_TEXTURE2DARRAY || dim == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY)
            d->srv.Texture2D.MipLevels = rest(d->srv.Texture2D.MipLevels, r->mips, d->srv.Texture2D.MostDetailedMip);
        if (dim == D3D11_SRV_DIMENSION_TEXTURE1DARRAY || dim == D3D11_SRV_DIMENSION_TEXTURE2DARRAY)
            d->srv.Texture2DArray.ArraySize = rest(d->srv.Texture2DArray.ArraySize, r->array, d->srv.Texture2DArray.FirstArraySlice);
        if (dim == D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY)
            d->srv.Texture2DMSArray.ArraySize = rest(d->srv.Texture2DMSArray.ArraySize, r->array, d->srv.Texture2DMSArray.FirstArraySlice);
        if (dim == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY && d->srv.TextureCubeArray.NumCubes == ~0u)
            RT_UNIMPLEMENTED(ctx, "a %s of a cube array with NumCubes 0xffffffff (first face %u of %u slices)", cls->name,
                             (unsigned)d->srv.TextureCubeArray.First2DArrayFace, (unsigned)r->array);
    } else if (v == 1 || v == 2) {      /* the two descriptions agree in the members used here */
        if (dim == D3D11_RTV_DIMENSION_TEXTURE1DARRAY || dim == D3D11_RTV_DIMENSION_TEXTURE2DARRAY)
            d->rtv.Texture2DArray.ArraySize = rest(d->rtv.Texture2DArray.ArraySize, r->array, d->rtv.Texture2DArray.FirstArraySlice);
        if (v == 2 && dim == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY)
            d->rtv.Texture2DMSArray.ArraySize = rest(d->rtv.Texture2DMSArray.ArraySize, r->array, d->rtv.Texture2DMSArray.FirstArraySlice);
        if (dim == D3D11_RTV_DIMENSION_TEXTURE3D)
            d->rtv.Texture3D.WSize = rest(d->rtv.Texture3D.WSize, level_depth(r->depth, d->rtv.Texture3D.MipSlice), d->rtv.Texture3D.FirstWSlice);
    } else {
        if (dim == D3D11_DSV_DIMENSION_TEXTURE1DARRAY || dim == D3D11_DSV_DIMENSION_TEXTURE2DARRAY)
            d->dsv.Texture2DArray.ArraySize = rest(d->dsv.Texture2DArray.ArraySize, r->array, d->dsv.Texture2DArray.FirstArraySlice);
        if (dim == D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY)
            d->dsv.Texture2DMSArray.ArraySize = rest(d->dsv.Texture2DMSArray.ArraySize, r->array, d->dsv.Texture2DMSArray.FirstArraySlice);
    }
}

/* The description a NULL stands for; false where no test read shows one (the head of this section). */
static bool view_derive(unsigned v, const ViewOf *r, ViewDesc *d) {
    if (r->kind < 2 || r->samples != 1) return false;
    if (r->kind == 2 && r->array != 1) return false;
    if (r->kind == 4 && v != 0) return false;
    if (!view_dims[v][r->kind]) return false;
    bool array = r->kind == 3 && r->array > 1;
    d->srv.Format = r->format;
    if (v == 0) {
        d->srv.Texture2D.MostDetailedMip = 0;
        d->srv.Texture2D.MipLevels = r->mips;
        if (r->kind == 2) d->srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
        else if (r->kind == 4) d->srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        else if (r->cube && r->array >= 12) {
            d->srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
            d->srv.TextureCubeArray.First2DArrayFace = 0;
            d->srv.TextureCubeArray.NumCubes = r->array / 6;
        } else if (r->cube) d->srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        else if (array) {
            d->srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            d->srv.Texture2DArray.FirstArraySlice = 0;
            d->srv.Texture2DArray.ArraySize = r->array;
        } else d->srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    } else if (v == 3) {                /* mip level 0, Flags 0: the zeroed description, with its dimension */
        d->dsv.ViewDimension = r->kind == 2 ? D3D11_DSV_DIMENSION_TEXTURE1D : array ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
        if (array) d->dsv.Texture2DArray.ArraySize = r->array;
    } else {                            /* the UAV's numbers and members are the RTV's */
        d->rtv.ViewDimension = r->kind == 2 ? D3D11_RTV_DIMENSION_TEXTURE1D : array ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
        if (array) d->rtv.Texture2DArray.ArraySize = r->array;
    }
    return true;
}

/* WHAT WINDOWS REFUSES IN A VIEW (unit d3d-fix-3; G6 of docs/reviews/d3d-code-review.md and
 * finding 2 of docs/reviews/night-2026-10-11-code-review.md).  Each is a stop that prints the
 * description and the resource: Windows answers E_INVALIDARG (Wine's test_create_*_view, as
 * the earlier check read them), which the game does not expect from a creator.  Microsoft's
 * pages, read 2026-10-11:
 *   THE BIND FLAG.  CreateShaderResourceView, pResource: "This resource must have been created
 *     with the D3D11_BIND_SHADER_RESOURCE flag."  CreateRenderTargetView: "This resource must
 *     have been created with the D3D11_BIND_RENDER_TARGET flag."  CreateDepthStencilView: "This
 *     resource must have been created with the D3D11_BIND_DEPTH_STENCIL flag."
 *     CreateUnorderedAccessView's page names no flag: D3D11_BIND_UNORDERED_ACCESS is from
 *     memory.  This is what keeps the device's record of who wrote a resource true: only a
 *     resource created for output (NdMemory.output) can be drawn, cleared or computed into.
 *   A TYPELESS FORMAT.  D3D11_RENDER_TARGET_VIEW_DESC: "A render-target-view cannot use the
 *     following formats: Any typeless format."; "If the format is set to DXGI_FORMAT_UNKNOWN,
 *     then the format of the resource that the view binds to the pipeline will be used."
 *     D3D11_DEPTH_STENCIL_VIEW_DESC: "A depth-stencil view cannot use a typeless format. If the
 *     format chosen is DXGI_FORMAT_UNKNOWN, then the format of the parent resource is used."
 *     D3D11_SHADER_RESOURCE_VIEW_DESC: "the resource-view description must specify a typed
 *     format, that is compatible with the resource format. So that means that you cannot create
 *     a resource-view description using any format with _TYPELESS in the name."  (The recording
 *     contradicts the last half-sentence for R24_UNORM_X8_TYPELESS: memory.c,
 *     rt_nulldev_format_typeless.)  For an unordered-access view: from memory.
 *     So a NULL description, or the format UNKNOWN, on a typeless texture is refused too: both
 *     stand for the resource's format (the pages above; CreateDepthStencilView, pDesc NULL:
 *     "using the format the resource was created with"), which no view can use.  This step is
 *     a reading of two sentences together, not a sentence of its own.
 *   THE FAMILY.  The same page: "You can however view a typeless resource by specifying a typed
 *     format for the view ... since these typed formats are compatible with the typeless
 *     resource."  From memory: a view of a resource that has a typed format must have that
 *     very format (R8G8B8A8_UNORM is not viewed as R8G8B8A8_UNORM_SRGB).
 *   A DEPTH-STENCIL VIEW'S FORMAT.  D3D11_DEPTH_STENCIL_VIEW_DESC: "These are valid formats for a
 *     depth-stencil view: DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT,
 *     DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_UNKNOWN".
 *   THE RANGE.  D3D11_TEX2D_SRV, MostDetailedMip: "this number is between 0 and MipLevels (from
 *     the original Texture2D for which ID3D11Device::CreateShaderResourceView creates a view)
 *     -1"; MipLevels: "Set to -1 to indicate all the mipmap levels from MostDetailedMip on down
 *     to least detailed."  From memory, by the same rule: a count of 0, a first level plus a
 *     count beyond the texture's levels, a MipSlice beyond them, array slices, cube faces and
 *     depth slices beyond what the texture has.
 * Not checked: the elements of a buffer view against the buffer's size, and a buffer view's
 * format (the brief names neither; the recording's 1,602 buffer views pass as they are).
 * False when the view is in order; else `why` says what is not. */
static bool view_problem(unsigned v, const ViewOf *r, const ViewDesc *d, bool given, char *why, size_t n) {
    static const UINT need[4] = { D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_UNORDERED_ACCESS, D3D11_BIND_RENDER_TARGET, D3D11_BIND_DEPTH_STENCIL };
    UINT f = d->srv.Format, dim = d->srv.ViewDimension;
    if (!(r->bind & need[v]))
        return snprintf(why, n, "its resource was created without the bind flag 0x%x of this kind of view", (unsigned)need[v]), true;
    if (r->kind == 1) return false;
    if (rt_nulldev_format_typeless(f))
        return snprintf(why, n, "%s typeless format, which no view can use", !given ? "a NULL description on a resource of a" :
                        f == r->format ? "the resource's own" : "a"), true;
    if (rt_nulldev_format_typeless(r->format) ? rt_nulldev_format_family(f) != r->format : f != r->format)
        return snprintf(why, n, "a format that is not %s", rt_nulldev_format_typeless(r->format) ? "of the family of its resource's typeless format" :
                        "the typed format its resource was created with"), true;
    if (v == 3 && f != DXGI_FORMAT_D16_UNORM && f != DXGI_FORMAT_D24_UNORM_S8_UINT && f != DXGI_FORMAT_D32_FLOAT && f != DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
        return snprintf(why, n, "a format that is not one of the four depth formats"), true;
    /* the ranges; the description is normalised, so no count is ~0u any more */
    UINT mip = 0, mips = 1, first = 0, count = 1, slices = r->array;
    if (v == 0) {
        if (dim == D3D11_SRV_DIMENSION_TEXTURE2DMS) return false;
        if (dim == D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY) { first = d->srv.Texture2DMSArray.FirstArraySlice; count = d->srv.Texture2DMSArray.ArraySize; }
        else {
            mip = d->srv.Texture2D.MostDetailedMip; mips = d->srv.Texture2D.MipLevels;
            if (dim == D3D11_SRV_DIMENSION_TEXTURE1DARRAY || dim == D3D11_SRV_DIMENSION_TEXTURE2DARRAY) {
                first = d->srv.Texture2DArray.FirstArraySlice; count = d->srv.Texture2DArray.ArraySize;
            } else if (dim == D3D11_SRV_DIMENSION_TEXTURECUBE) {
                count = 6;
            } else if (dim == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY) {
                first = d->srv.TextureCubeArray.First2DArrayFace;
                if (!d->srv.TextureCubeArray.NumCubes || d->srv.TextureCubeArray.NumCubes > slices / 6)
                    return snprintf(why, n, "%u cubes, of a texture of %u slices", (unsigned)d->srv.TextureCubeArray.NumCubes, (unsigned)slices), true;
                count = d->srv.TextureCubeArray.NumCubes * 6;
            }
        }
    } else {                            /* MipSlice is the first member of every texture member of the three other descriptions */
        bool dsv = v == 3;
        UINT ms = dsv ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2DMS;      /* the UAV has no such dimension */
        UINT msarray = dsv ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
        UINT a1 = dsv ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY : D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
        UINT a2 = dsv ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        if (v != 1 && dim == ms) return false;
        if (v != 1 && dim == msarray) {
            first = dsv ? d->dsv.Texture2DMSArray.FirstArraySlice : d->rtv.Texture2DMSArray.FirstArraySlice;
            count = dsv ? d->dsv.Texture2DMSArray.ArraySize : d->rtv.Texture2DMSArray.ArraySize;
        } else {
            mip = dsv ? d->dsv.Texture2D.MipSlice : d->rtv.Texture2D.MipSlice;
            if (dim == a1 || dim == a2) {
                first = dsv ? d->dsv.Texture2DArray.FirstArraySlice : d->rtv.Texture2DArray.FirstArraySlice;
                count = dsv ? d->dsv.Texture2DArray.ArraySize : d->rtv.Texture2DArray.ArraySize;
            } else if (!dsv && dim == D3D11_RTV_DIMENSION_TEXTURE3D) {
                first = d->rtv.Texture3D.FirstWSlice; count = d->rtv.Texture3D.WSize;
                slices = level_depth(r->depth, mip);
            }
        }
    }
    if (!mips || mip >= r->mips || mips > r->mips - mip)
        return snprintf(why, n, "mip levels %u to %u, of a texture of %u", (unsigned)mip, (unsigned)(mip + mips), (unsigned)r->mips), true;
    if (!count || first >= slices || count > slices - first)
        return snprintf(why, n, "slices %u to %u, of %u", (unsigned)first, (unsigned)(first + count), (unsigned)slices), true;
    return false;
}

HRESULT rt_nulldev_view_create(Ctx *ctx, ID3D11Device *device, const RtComClass *cls, ID3D11Resource *resource,
                               const void *desc, void **out) {
    (void)rt_com_state(ctx, device, &rt_nulldev_device_class);        /* a stop that names the method when This is no device (G8) */
    unsigned v = cls == &rt_nulldev_srv_class ? 0 : cls == &rt_nulldev_uav_class ? 1 : cls == &rt_nulldev_rtv_class ? 2 : 3;
    static const uint32_t bytes[4] = { sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC), sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC),
                                       sizeof(D3D11_RENDER_TARGET_VIEW_DESC), sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC) };
    ViewDesc d;
    if (v == 3 && cls != &rt_nulldev_dsv_class) rt_fatal("rt_nulldev: class %s is not a view class", cls ? cls->name : "(null)");
    if (!resource) RT_UNIMPLEMENTED(ctx, "a view on a NULL resource");
    resource = rt_nulldev_resource_arg(ctx, resource, "pResource");
    const RtComClass *rc = rt_com_class_of(resource);
    ViewOf r = { .kind = rc == &rt_nulldev_buffer_class ? 1 : rc == &rt_nulldev_texture1d_class ? 2 : rc == &rt_nulldev_texture2d_class ? 3 : 4,
                 .array = 1, .depth = 1, .samples = 1 };
    if (r.kind > 1) {
        const NdTexture *t = rt_com_state_of(resource, rc);
        if (r.kind == 2) {
            r.format = t->desc.d1.Format; r.mips = t->desc.d1.MipLevels; r.array = t->desc.d1.ArraySize;
            r.bind = t->desc.d1.BindFlags; r.usage = t->desc.d1.Usage;
        } else if (r.kind == 3) {
            r.format = t->desc.d2.Format; r.mips = t->desc.d2.MipLevels; r.array = t->desc.d2.ArraySize;
            r.samples = t->desc.d2.SampleDesc.Count; r.cube = (t->desc.d2.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
            r.bind = t->desc.d2.BindFlags; r.usage = t->desc.d2.Usage;
        } else {
            r.format = t->desc.d3.Format; r.mips = t->desc.d3.MipLevels; r.depth = t->desc.d3.Depth;
            r.bind = t->desc.d3.BindFlags; r.usage = t->desc.d3.Usage;
        }
    } else {
        const NdBuffer *b = rt_com_state_of(resource, rc);
        r.bind = b->desc.BindFlags; r.usage = b->desc.Usage;
    }
    memset(&d, 0, sizeof d);
    if (desc) {
        uint32_t b, blk;
        memcpy(&d, desc, bytes[v]);
        if (d.srv.ViewDimension > 11 || !(view_dims[v][r.kind] >> d.srv.ViewDimension & 1))
            RT_UNIMPLEMENTED(ctx, "a %s with view dimension %u, which is not one of a %s's (format %u)", cls->name,
                             (unsigned)d.srv.ViewDimension, kind_name[r.kind], (unsigned)d.srv.Format);
        view_normalise(ctx, cls, v, &r, &d);
        if (d.srv.Format != DXGI_FORMAT_UNKNOWN && !rt_nulldev_format_bytes(d.srv.Format, &b, &blk))
            RT_UNIMPLEMENTED(ctx, "a %s with format %u, which has no size in the format table", cls->name, (unsigned)d.srv.Format);
    } else if (!view_derive(v, &r, &d)) {
        RT_UNIMPLEMENTED(ctx, "a %s with a NULL description on a %s that is not a plain texture (a buffer, a multisampled "
                         "texture, a 1D array, or a 3D texture for a view that is not a shader-resource view)",
                         cls->name, kind_name[r.kind]);
    }
    char why[160];
    if (view_problem(v, &r, &d, desc != NULL, why, sizeof why)) {
        const UINT *u = (const UINT *)&d + (v == 3 ? 3 : 2);        /* the union's first words: what the dimension makes of them */
        RT_UNIMPLEMENTED(ctx, "a %s with %s: Windows refuses it.  The description%s: format %u, view dimension %u, then %u, %u, %u, %u; "
                         "the resource: a %s, format %u, %u mip levels, %u slices, depth %u, usage %u, bind flags 0x%x",
                         cls->name, why, desc ? "" : " a NULL stands for", (unsigned)d.srv.Format, (unsigned)d.srv.ViewDimension,
                         (unsigned)u[0], (unsigned)u[1], (unsigned)u[2], (unsigned)u[3], kind_name[r.kind], (unsigned)r.format,
                         (unsigned)r.mips, (unsigned)r.array, (unsigned)r.depth, (unsigned)r.usage, (unsigned)r.bind);
    }
    if (!out) return S_FALSE;
    void *view = rt_nulldev_new(device, cls);
    NdView *w = rt_com_state_of(view, cls);
    rt_nulldev_bind(&w->resource, resource);
    memcpy(&w->desc, &d, bytes[v]);
    *out = view;
    return S_OK;
}

/* ---- shaders and the input layout ----------------------------------------------------------------
 * Microsoft, CreateVertexShader (read 2026-10-10 by a subagent): pClassLinkage "the value can be
 * NULL"; ppVertexShader: "If this is NULL, all other parameters will be validated, and if all
 * parameters pass validation this API will return S_FALSE instead of S_OK."  From memory: a
 * compiled shader begins with the four characters "DXBC". */

HRESULT rt_nulldev_shader_create(Ctx *ctx, ID3D11Device *device, const RtComClass *cls, const void *bytecode,
                                 SIZE_T length, void *class_linkage, void **out) {
    (void)rt_com_state(ctx, device, &rt_nulldev_device_class);        /* a stop that names the method when This is no device (G8) */
    if (cls != &rt_nulldev_vs_class && cls != &rt_nulldev_ps_class && cls != &rt_nulldev_gs_class && cls != &rt_nulldev_cs_class)
        rt_fatal("rt_nulldev: class %s is not a shader class", cls ? cls->name : "(null)");
    if (class_linkage) RT_UNIMPLEMENTED(ctx, "a %s with a class linkage (%p)", cls->name, class_linkage);
    if (!bytecode || length < 4 || memcmp(bytecode, "DXBC", 4))
        RT_UNIMPLEMENTED(ctx, "a %s whose bytecode (%p, %llu bytes) is NULL, shorter than 4 bytes or does not begin with DXBC",
                         cls->name, bytecode, (unsigned long long)length);
    if (!out) return S_FALSE;
    void *shader = rt_nulldev_new(device, cls);
    ((NdShader *)rt_com_state_of(shader, cls))->bytecode_length = length;
    *out = shader;
    return S_OK;
}

void rt_nulldev_inputlayout_set(ID3D11InputLayout *layout, const D3D11_INPUT_ELEMENT_DESC *elements, UINT n) {
    NdInputLayout *l = rt_com_state_of(layout, &rt_nulldev_inputlayout_class);
    free(l->elements);
    l->elements = n ? malloc(n * sizeof *elements) : NULL;
    if (n && !l->elements) rt_fatal("rt_nulldev: no host memory for an input layout's %u elements", (unsigned)n);
    if (n) memcpy(l->elements, elements, n * sizeof *elements);
    l->nelements = n;
}

void rt_nulldev_inputlayout_free(NdInputLayout *l) {
    free(l->elements);
    l->elements = NULL;
}

/* ---- the swap chain's back buffer ------------------------------------------------------------------
 * From memory: DXGI_USAGE_RENDER_TARGET_OUTPUT, _SHADER_INPUT and _UNORDERED_ACCESS give the
 * back buffer the bind flags RENDER_TARGET, SHADER_RESOURCE and UNORDERED_ACCESS; it is a
 * DEFAULT-usage texture with one mip level and one slice. */
void rt_nulldev_swapchain_buffer(Ctx *ctx, IDXGISwapChain *sc) {
    NdSwapChain *s = rt_com_state_of(sc, &rt_nulldev_swapchain_class);
    D3D11_TEXTURE2D_DESC d;
    memset(&d, 0, sizeof d);
    d.Width = s->desc.BufferDesc.Width;
    d.Height = s->desc.BufferDesc.Height;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = s->desc.BufferDesc.Format;
    d.SampleDesc = s->desc.SampleDesc;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = (s->desc.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT ? D3D11_BIND_RENDER_TARGET : 0) |
                  (s->desc.BufferUsage & DXGI_USAGE_SHADER_INPUT ? D3D11_BIND_SHADER_RESOURCE : 0) |
                  (s->desc.BufferUsage & DXGI_USAGE_UNORDERED_ACCESS ? D3D11_BIND_UNORDERED_ACCESS : 0);
    ID3D11Texture2D *fresh = rt_nulldev_texture_new(ctx, s->device, D3D11_RESOURCE_DIMENSION_TEXTURE2D, &d, false);
    ID3D11Texture2D *old = s->back_buffer;
    rt_com_hold(fresh);                 /* the swap chain's own, which the game does not see: after one GetBuffer the */
    rt_com_release(fresh);              /* buffer's count is 1 (Wine, test_swapchain_views, through the check's F3) */
    s->back_buffer = fresh;
    if (old) rt_com_unhold(old);
    /* The seam (rt/d3dback.h, rule 3): the back end's swap chain hands out its buffer 0, which
     * becomes the back object of the front's back buffer; views of it are made like any view. */
    RT_BACK_CREATE(ctx, fresh, SwapChainGetBuffer, s->back, 0);
}
