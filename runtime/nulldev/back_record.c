/* The recording back end (rt/d3dback.h): stands where a renderer would, counts every entry,
 * keeps the last arguments of each and refuses whatever is not its own.  For the tests of the
 * seam and for `make d3d-replay-back`; RT_D3D_BACKEND=record.
 *
 * Its objects are small blocks of host memory that are never given back (a released one stays
 * marked dead, so a pointer that crosses after its Release is caught, as the front does it).
 * Counts are atomic; the last arguments are plain memory: a test reads them on the thread
 * that made the call. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/log.h"
#include "rt/nulldev.h"

#define REC_MAGIC 0x6b636142u                   /* "Back" */
#define REC_ARGS  9u
#define REC_ARRAY 128u                          /* the longest array a method takes: ND_SRV_SLOTS */

typedef struct RecMap { UINT sub; void *raw; uint8_t *data; struct RecMap *next; } RecMap;

typedef struct RecObj {
    uint32_t magic, kind;
    _Atomic uint32_t live;
    uint64_t serial;
    union { D3D11_BUFFER_DESC b; D3D11_TEXTURE1D_DESC d1; D3D11_TEXTURE2D_DESC d2; D3D11_TEXTURE3D_DESC d3; } desc;
    bool     sized;                             /* a description was kept: the resource can be mapped */
    RecMap  *maps;                              /* the memory Map handed out, one piece per subresource */
} RecObj;

typedef struct RecEntry {
    _Atomic uint64_t count;
    _Atomic int32_t  answer;                    /* what an HRESULT entry returns */
    uint64_t arg[REC_ARGS];
    struct { int arg; unsigned n; void *obj[REC_ARRAY]; } array[2];
} RecEntry;

static RecEntry rec[RT_BACK_N];
static _Atomic uint64_t made[RT_BK_KINDS], live[RT_BK_KINDS], serials;
static uint8_t query_data[16];

static const char *const kind_name[RT_BK_KINDS] = {
    "device", "context", "swap chain", "buffer", "texture 1D", "texture 2D", "texture 3D", "shader-resource view",
    "unordered-access view", "render-target view", "depth-stencil view", "blend state", "depth-stencil state", "rasterizer state",
    "sampler state", "vertex shader", "pixel shader", "geometry shader", "compute shader", "input layout", "query",
};

#define K(k)      (1u << RT_BK_##k)
#define RESOURCE  (K(BUFFER) | K(TEXTURE1D) | K(TEXTURE2D) | K(TEXTURE3D))

/* Rule 2 of rt/d3dback.h: what crosses for an object is NULL or a live object of ours, of a
 * kind the entry takes.  (A front object that crossed by mistake is readable memory with
 * another first word; anything else faults here, which is as loud.) */
static void *check(const char *entry, unsigned i, const void *p, unsigned kinds) {
    const RecObj *o = p;
    if (!o) return NULL;
    if (o->magic != REC_MAGIC)
        rt_fatal("d3dback record: %s: argument %u (%p) is not an object of the back end: a front pointer crossed, or garbage "
                 "(rt/d3dback.h, rule 2)", entry, i, p);
    if (!atomic_load(&o->live))
        rt_fatal("d3dback record: %s: argument %u is back object #%llu (%s), which was released", entry, i,
                 (unsigned long long)o->serial, kind_name[o->kind]);
    if (!(kinds & (1u << o->kind)))
        rt_fatal("d3dback record: %s: argument %u is back object #%llu, a %s, which the entry does not take there", entry, i,
                 (unsigned long long)o->serial, kind_name[o->kind]);
    return (void *)(uintptr_t)p;
}

static void *new_object(RtD3dBackKind kind) {
    RecObj *o = calloc(1, sizeof *o);
    if (!o) rt_fatal("d3dback record: no host memory for an object");
    o->magic = REC_MAGIC;
    o->kind = kind;
    o->serial = atomic_fetch_add(&serials, 1) + 1;
    atomic_store(&o->live, 1);
    atomic_fetch_add(&made[kind], 1);
    atomic_fetch_add(&live[kind], 1);
    return o;
}

static void keep_array(RecEntry *r, const char *entry, unsigned i, unsigned which, void *const *a, UINT n, unsigned kinds) {
    r->arg[i] = (uint64_t)(uintptr_t)a;
    r->array[which].arg = (int)i;
    r->array[which].n = a ? n : 0;
    for (UINT j = 0; a && j < n; j++) {
        void *o = check(entry, i, a[j], kinds);
        if (j < REC_ARRAY) r->array[which].obj[j] = o;
    }
}

/* What every entry begins with: count it; `r_` and `en_` are in scope for the argument macros. */
#define E(entry)            RecEntry *r_ = &rec[RT_BACK_##entry]; const char *en_ __attribute__((unused)) = #entry; atomic_fetch_add(&r_->count, 1); \
                            r_->array[0].arg = r_->array[1].arg = -1
#define V(i, v)             r_->arg[i] = (uint64_t)(v)
#define P(i, p)             r_->arg[i] = (uint64_t)(uintptr_t)(p)
#define O(i, o, kinds)      r_->arg[i] = (uint64_t)(uintptr_t)check(en_, i, (o), (kinds))
#define A(i, w, a, n, kinds) keep_array(r_, en_, i, w, (a), (n), (kinds))
#define F(i, f)             do { float f_ = (f); uint32_t b_; memcpy(&b_, &f_, 4); r_->arg[i] = b_; } while (0)
#define ANSWER()            ((HRESULT)atomic_load(&r_->answer))
/* A creator's end: an object of `kind` through out-pointer `out`, argument `i`, unless the entry was told to fail. */
#define MADE(i, out, kind)  do { if (ANSWER() != S_OK) { r_->arg[i] = 0; return ANSWER(); } \
                                 *(out) = new_object(kind); r_->arg[i] = (uint64_t)(uintptr_t)*(out); return S_OK; } while (0)

/* ---- the seam's own ---- */

static HRESULT r_CreateDevice(void **device, void **context) {
    E(CreateDevice);
    if (ANSWER() != S_OK) return ANSWER();
    *device = new_object(RT_BK_DEVICE);
    *context = new_object(RT_BK_CONTEXT);
    P(0, *device); P(1, *context);
    return S_OK;
}

static void r_Release(RtD3dBackKind kind, void *object) {
    E(Release);
    V(0, kind);
    RecObj *o = check(en_, 1, object, kind < RT_BK_KINDS ? 1u << kind : 0);
    P(1, o);
    if (!o) rt_fatal("d3dback record: Release of NULL");
    atomic_store(&o->live, 0);
    atomic_fetch_sub(&live[kind], 1);
    for (RecMap *m = o->maps, *next; m; m = next) { next = m->next; free(m->raw); free(m); }
    o->maps = NULL;
}

static HRESULT r_SwapChainGetBuffer(void *swapchain, UINT buffer, void **texture) {
    E(SwapChainGetBuffer); O(0, swapchain, K(SWAPCHAIN)); V(1, buffer);
    MADE(2, texture, RT_BK_TEXTURE2D);
}

/* ---- creators ---- */

static HRESULT r_CreateBuffer(void *device, const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **buffer) {
    E(CreateBuffer); O(0, device, K(DEVICE)); P(1, desc); P(2, initial);
    if (ANSWER() != S_OK) { r_->arg[3] = 0; return ANSWER(); }
    RecObj *o = new_object(RT_BK_BUFFER);
    o->desc.b = *desc; o->sized = true;
    *buffer = o; P(3, o);
    return S_OK;
}

#define TEXTURE(n, kind_, member) \
    static HRESULT r_CreateTexture##n(void *device, const D3D11_TEXTURE##n##_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **texture) { \
        E(CreateTexture##n); O(0, device, K(DEVICE)); P(1, desc); P(2, initial); \
        if (ANSWER() != S_OK) { r_->arg[3] = 0; return ANSWER(); } \
        RecObj *o = new_object(kind_); \
        o->desc.member = *desc; o->sized = true; \
        *texture = o; P(3, o); \
        return S_OK; \
    }
TEXTURE(1D, RT_BK_TEXTURE1D, d1)
TEXTURE(2D, RT_BK_TEXTURE2D, d2)
TEXTURE(3D, RT_BK_TEXTURE3D, d3)

#define VIEW(name, desc_type, kind_) \
    static HRESULT r_Create##name(void *device, void *resource, const desc_type *desc, void **view) { \
        E(Create##name); O(0, device, K(DEVICE)); O(1, resource, RESOURCE); P(2, desc); \
        MADE(3, view, kind_); \
    }
VIEW(ShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC, RT_BK_SRV)
VIEW(UnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC, RT_BK_UAV)
VIEW(RenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC, RT_BK_RTV)
VIEW(DepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC, RT_BK_DSV)

static HRESULT r_CreateInputLayout(void *device, const D3D11_INPUT_ELEMENT_DESC *elements, UINT count, const void *bytecode, SIZE_T length, void **layout) {
    E(CreateInputLayout); O(0, device, K(DEVICE)); P(1, elements); V(2, count); P(3, bytecode); V(4, length);
    MADE(5, layout, RT_BK_INPUTLAYOUT);
}

#define SHADER(name, kind_) \
    static HRESULT r_Create##name(void *device, const void *bytecode, SIZE_T length, void **shader) { \
        E(Create##name); O(0, device, K(DEVICE)); P(1, bytecode); V(2, length); \
        MADE(3, shader, kind_); \
    }
SHADER(VertexShader, RT_BK_VS)
SHADER(GeometryShader, RT_BK_GS)
SHADER(PixelShader, RT_BK_PS)
SHADER(ComputeShader, RT_BK_CS)

#define STATE(name, desc_type, kind_) \
    static HRESULT r_Create##name(void *device, const desc_type *desc, void **state) { \
        E(Create##name); O(0, device, K(DEVICE)); P(1, desc); \
        MADE(2, state, kind_); \
    }
STATE(BlendState, D3D11_BLEND_DESC, RT_BK_BLEND)
STATE(DepthStencilState, D3D11_DEPTH_STENCIL_DESC, RT_BK_DEPTHSTENCIL)
STATE(RasterizerState, D3D11_RASTERIZER_DESC, RT_BK_RASTERIZER)
STATE(SamplerState, D3D11_SAMPLER_DESC, RT_BK_SAMPLER)
STATE(Query, D3D11_QUERY_DESC, RT_BK_QUERY)

static HRESULT r_SetMaximumFrameLatency(void *device, UINT latency) {
    E(SetMaximumFrameLatency); O(0, device, K(DEVICE)); V(1, latency);
    return ANSWER();
}

/* ---- the swap chain ---- */

static HRESULT r_CreateSwapChain(void *device, const DXGI_SWAP_CHAIN_DESC *desc, void **swapchain) {
    E(CreateSwapChain); O(0, device, K(DEVICE)); P(1, desc);
    MADE(2, swapchain, RT_BK_SWAPCHAIN);
}

static HRESULT r_ResizeBuffers(void *swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    E(ResizeBuffers); O(0, swapchain, K(SWAPCHAIN)); V(1, count); V(2, width); V(3, height); V(4, format); V(5, flags);
    return ANSWER();
}

static HRESULT r_Present(void *swapchain, UINT sync_interval, UINT flags) {
    E(Present); O(0, swapchain, K(SWAPCHAIN)); V(1, sync_interval); V(2, flags);
    return ANSWER();
}

/* ---- the context: binds ---- */

#define BIND_ARRAY(name, kinds) \
    static void r_##name(void *context, UINT start, UINT count, void *const *objects) { \
        E(name); O(0, context, K(CONTEXT)); V(1, start); V(2, count); A(3, 0, objects, count, kinds); \
    }
BIND_ARRAY(VSSetConstantBuffers, K(BUFFER))
BIND_ARRAY(PSSetConstantBuffers, K(BUFFER))
BIND_ARRAY(GSSetConstantBuffers, K(BUFFER))
BIND_ARRAY(CSSetConstantBuffers, K(BUFFER))
BIND_ARRAY(VSSetShaderResources, K(SRV))
BIND_ARRAY(PSSetShaderResources, K(SRV))
BIND_ARRAY(GSSetShaderResources, K(SRV))
BIND_ARRAY(CSSetShaderResources, K(SRV))
BIND_ARRAY(VSSetSamplers, K(SAMPLER))
BIND_ARRAY(PSSetSamplers, K(SAMPLER))
BIND_ARRAY(GSSetSamplers, K(SAMPLER))
BIND_ARRAY(CSSetSamplers, K(SAMPLER))

static void r_IASetVertexBuffers(void *context, UINT start, UINT count, void *const *buffers, const UINT *strides, const UINT *offsets) {
    E(IASetVertexBuffers); O(0, context, K(CONTEXT)); V(1, start); V(2, count); A(3, 0, buffers, count, K(BUFFER)); P(4, strides); P(5, offsets);
}

static void r_CSSetUnorderedAccessViews(void *context, UINT start, UINT count, void *const *views, const UINT *initial_counts) {
    E(CSSetUnorderedAccessViews); O(0, context, K(CONTEXT)); V(1, start); V(2, count); A(3, 0, views, count, K(UAV)); P(4, initial_counts);
}

#define BIND_ONE(name, kinds) \
    static void r_##name(void *context, void *object) { E(name); O(0, context, K(CONTEXT)); O(1, object, kinds); }
BIND_ONE(VSSetShader, K(VS))
BIND_ONE(PSSetShader, K(PS))
BIND_ONE(GSSetShader, K(GS))
BIND_ONE(CSSetShader, K(CS))
BIND_ONE(HSSetShader, 0)                        /* no hull or domain shader exists: NULL only */
BIND_ONE(DSSetShader, 0)
BIND_ONE(IASetInputLayout, K(INPUTLAYOUT))
BIND_ONE(RSSetState, K(RASTERIZER))

static void r_IASetIndexBuffer(void *context, void *buffer, DXGI_FORMAT format, UINT offset) {
    E(IASetIndexBuffer); O(0, context, K(CONTEXT)); O(1, buffer, K(BUFFER)); V(2, format); V(3, offset);
}

static void r_OMSetBlendState(void *context, void *state, const FLOAT *factor, UINT sample_mask) {
    E(OMSetBlendState); O(0, context, K(CONTEXT)); O(1, state, K(BLEND)); P(2, factor); V(3, sample_mask);
}

static void r_OMSetDepthStencilState(void *context, void *state, UINT stencil_ref) {
    E(OMSetDepthStencilState); O(0, context, K(CONTEXT)); O(1, state, K(DEPTHSTENCIL)); V(2, stencil_ref);
}

static void r_OMSetRenderTargets(void *context, UINT count, void *const *views, void *depth_stencil_view) {
    E(OMSetRenderTargets); O(0, context, K(CONTEXT)); V(1, count); A(2, 0, views, count, K(RTV)); O(3, depth_stencil_view, K(DSV));
}

static void r_OMSetRenderTargetsAndUnorderedAccessViews(void *context, UINT rtv_count, void *const *rtvs, void *depth_stencil_view,
                                                        UINT uav_start, UINT uav_count, void *const *uavs, const UINT *initial_counts) {
    E(OMSetRenderTargetsAndUnorderedAccessViews); O(0, context, K(CONTEXT)); V(1, rtv_count);
    /* 0xffffffff keeps what is bound: the array is not read then */
    A(2, 0, rtvs, rtv_count == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL ? 0 : rtv_count, K(RTV));
    O(3, depth_stencil_view, K(DSV)); V(4, uav_start); V(5, uav_count);
    A(6, 1, uavs, uav_count == D3D11_KEEP_UNORDERED_ACCESS_VIEWS ? 0 : uav_count, K(UAV)); P(7, initial_counts);
}

/* ---- state, draws, clears, copies ---- */

static void r_IASetPrimitiveTopology(void *context, D3D11_PRIMITIVE_TOPOLOGY topology) { E(IASetPrimitiveTopology); O(0, context, K(CONTEXT)); V(1, topology); }
static void r_RSSetViewports(void *context, UINT count, const D3D11_VIEWPORT *viewports) { E(RSSetViewports); O(0, context, K(CONTEXT)); V(1, count); P(2, viewports); }

static void r_Draw(void *context, UINT vertex_count, UINT start_vertex) { E(Draw); O(0, context, K(CONTEXT)); V(1, vertex_count); V(2, start_vertex); }
static void r_DrawIndexed(void *context, UINT index_count, UINT start_index, INT base_vertex) {
    E(DrawIndexed); O(0, context, K(CONTEXT)); V(1, index_count); V(2, start_index); V(3, (int64_t)base_vertex);
}
static void r_DrawInstanced(void *context, UINT vertex_count, UINT instance_count, UINT start_vertex, UINT start_instance) {
    E(DrawInstanced); O(0, context, K(CONTEXT)); V(1, vertex_count); V(2, instance_count); V(3, start_vertex); V(4, start_instance);
}
static void r_DrawIndexedInstanced(void *context, UINT index_count, UINT instance_count, UINT start_index, INT base_vertex, UINT start_instance) {
    E(DrawIndexedInstanced); O(0, context, K(CONTEXT)); V(1, index_count); V(2, instance_count); V(3, start_index); V(4, (int64_t)base_vertex); V(5, start_instance);
}
static void r_DrawInstancedIndirect(void *context, void *buffer, UINT offset) { E(DrawInstancedIndirect); O(0, context, K(CONTEXT)); O(1, buffer, K(BUFFER)); V(2, offset); }
static void r_Dispatch(void *context, UINT x, UINT y, UINT z) { E(Dispatch); O(0, context, K(CONTEXT)); V(1, x); V(2, y); V(3, z); }
static void r_DispatchIndirect(void *context, void *buffer, UINT offset) { E(DispatchIndirect); O(0, context, K(CONTEXT)); O(1, buffer, K(BUFFER)); V(2, offset); }

static void r_ClearRenderTargetView(void *context, void *view, const FLOAT *rgba) { E(ClearRenderTargetView); O(0, context, K(CONTEXT)); O(1, view, K(RTV)); P(2, rgba); }
static void r_ClearDepthStencilView(void *context, void *view, UINT flags, FLOAT depth, UINT8 stencil) {
    E(ClearDepthStencilView); O(0, context, K(CONTEXT)); O(1, view, K(DSV)); V(2, flags); F(3, depth); V(4, stencil);
}
static void r_ClearUnorderedAccessViewFloat(void *context, void *view, const FLOAT *values) {
    E(ClearUnorderedAccessViewFloat); O(0, context, K(CONTEXT)); O(1, view, K(UAV)); P(2, values);
}

static void r_CopyResource(void *context, void *dst, void *src) { E(CopyResource); O(0, context, K(CONTEXT)); O(1, dst, RESOURCE); O(2, src, RESOURCE); }
static void r_CopySubresourceRegion(void *context, void *dst, UINT dst_sub, UINT x, UINT y, UINT z, void *src, UINT src_sub, const D3D11_BOX *box) {
    E(CopySubresourceRegion); O(0, context, K(CONTEXT)); O(1, dst, RESOURCE); V(2, dst_sub); V(3, x); V(4, y); V(5, z); O(6, src, RESOURCE); V(7, src_sub); P(8, box);
}
static void r_ResolveSubresource(void *context, void *dst, UINT dst_sub, void *src, UINT src_sub, DXGI_FORMAT format) {
    E(ResolveSubresource); O(0, context, K(CONTEXT)); O(1, dst, RESOURCE); V(2, dst_sub); O(3, src, RESOURCE); V(4, src_sub); V(5, format);
}
static void r_UpdateSubresource(void *context, void *dst, UINT dst_sub, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch) {
    E(UpdateSubresource); O(0, context, K(CONTEXT)); O(1, dst, RESOURCE); V(2, dst_sub); P(3, box); P(4, data); V(5, row_pitch); V(6, depth_pitch);
}

/* ---- mapping: host memory of the subresource's size, with the pitches the front computes ---- */

static bool level_of(const RecObj *o, UINT sub, NdLevel *l) {
    if (!o->sized) return false;
    if (o->kind == RT_BK_BUFFER) {
        memset(l, 0, sizeof *l);
        l->width = o->desc.b.ByteWidth; l->height = l->depth = l->rows = 1;
        l->row_bytes = o->desc.b.ByteWidth; l->slice_bytes = l->bytes = o->desc.b.ByteWidth;
        return sub == 0;
    }
    DXGI_FORMAT format;
    UINT w, h = 1, d = 1, mips;
    if (o->kind == RT_BK_TEXTURE1D) { format = o->desc.d1.Format; w = o->desc.d1.Width; mips = o->desc.d1.MipLevels; }
    else if (o->kind == RT_BK_TEXTURE2D) { format = o->desc.d2.Format; w = o->desc.d2.Width; h = o->desc.d2.Height; mips = o->desc.d2.MipLevels; }
    else { format = o->desc.d3.Format; w = o->desc.d3.Width; h = o->desc.d3.Height; d = o->desc.d3.Depth; mips = o->desc.d3.MipLevels; }
    if (!mips) mips = rt_nulldev_full_mips(w, h, d);
    return rt_nulldev_level(format, w, h, d, sub % mips, l);       /* subresource = level + slice * levels */
}

static HRESULT r_Map(void *context, void *resource, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped) {
    E(Map); O(0, context, K(CONTEXT)); O(1, resource, RESOURCE); V(2, sub); V(3, type); V(4, flags); P(5, mapped);
    if (ANSWER() != S_OK) return ANSWER();
    RecObj *o = resource;
    NdLevel l;
    if (!o || !level_of(o, sub, &l))
        rt_fatal("d3dback record: Map of subresource %u of a resource the back end cannot size (object #%llu)", (unsigned)sub,
                 o ? (unsigned long long)o->serial : 0ull);
    RecMap *m = o->maps;
    while (m && m->sub != sub) m = m->next;
    if (!m) {
        m = calloc(1, sizeof *m);
        void *raw = m ? calloc(1, (size_t)l.bytes + 32) : NULL;
        if (!raw) rt_fatal("d3dback record: no host memory for a mapping of %llu bytes", (unsigned long long)l.bytes);
        m->sub = sub;
        m->raw = raw;
        m->data = (uint8_t *)(((uintptr_t)raw + 15) & ~(uintptr_t)15);
        m->next = o->maps;
        o->maps = m;
    }
    P(6, m->data);                              /* for a test: what was handed out */
    mapped->pData = m->data;
    mapped->RowPitch = l.row_bytes;
    mapped->DepthPitch = (UINT)l.slice_bytes;
    return S_OK;
}

static void r_Unmap(void *context, void *resource, UINT sub) { E(Unmap); O(0, context, K(CONTEXT)); O(1, resource, RESOURCE); V(2, sub); }

/* ---- queries ---- */

static void r_Begin(void *context, void *query) { E(Begin); O(0, context, K(CONTEXT)); O(1, query, K(QUERY)); }
static void r_End(void *context, void *query) { E(End); O(0, context, K(CONTEXT)); O(1, query, K(QUERY)); }
static HRESULT r_GetData(void *context, void *query, void *data, UINT size, UINT flags) {
    E(GetData); O(0, context, K(CONTEXT)); O(1, query, K(QUERY)); P(2, data); V(3, size); V(4, flags);
    if (ANSWER() == S_OK && data && size) {
        memset(data, 0, size);
        memcpy(data, query_data, size < sizeof query_data ? size : sizeof query_data);
    }
    return ANSWER();
}

static void r_ClearState(void *context) { E(ClearState); O(0, context, K(CONTEXT)); }
static void r_Flush(void *context) { E(Flush); O(0, context, K(CONTEXT)); }

/* ---- the table ---- */

#define MEMBER_(entry, iface) .entry = r_##entry,
static const RtD3dBack record = { .name = "record", RT_D3DBACK_ENTRIES(MEMBER_) };
#undef MEMBER_

const RtD3dBack *rt_d3dback_record(void) { return &record; }

void *rt_d3dback_record_object(RtD3dBackKind kind) {
    if ((unsigned)kind >= RT_BK_KINDS) rt_fatal("d3dback record: kind %u", (unsigned)kind);
    return new_object(kind);
}

/* ---- what a test reads ---- */

static RecEntry *entry_of(RtD3dBackEntry entry) {
    if ((unsigned)entry >= RT_BACK_N) rt_fatal("d3dback record: entry %u", (unsigned)entry);
    return &rec[entry];
}

uint64_t rt_d3dback_record_count(RtD3dBackEntry entry) { return atomic_load(&entry_of(entry)->count); }

uint64_t rt_d3dback_record_arg(RtD3dBackEntry entry, unsigned i) {
    if (i >= REC_ARGS) rt_fatal("d3dback record: argument %u of %s", i, rt_d3dback_entry_name(entry));
    return entry_of(entry)->arg[i];
}

unsigned rt_d3dback_record_array(RtD3dBackEntry entry, unsigned i, void **out, unsigned max) {
    RecEntry *r = entry_of(entry);
    for (unsigned w = 0; w < 2; w++)
        if (r->array[w].arg == (int)i) {
            for (unsigned j = 0; j < r->array[w].n && j < max && j < REC_ARRAY; j++) out[j] = r->array[w].obj[j];
            return r->array[w].n;
        }
    rt_fatal("d3dback record: argument %u of the last %s was not an array of objects", i, rt_d3dback_entry_name(entry));
}

void rt_d3dback_record_answer(RtD3dBackEntry entry, HRESULT hr) { atomic_store(&entry_of(entry)->answer, (int32_t)hr); }

void rt_d3dback_record_data(const void *bytes, size_t n) {
    memset(query_data, 0, sizeof query_data);
    if (bytes) memcpy(query_data, bytes, n < sizeof query_data ? n : sizeof query_data);
}

uint64_t rt_d3dback_record_made(RtD3dBackKind kind) { return (unsigned)kind < RT_BK_KINDS ? atomic_load(&made[kind]) : 0; }
uint64_t rt_d3dback_record_live(RtD3dBackKind kind) { return (unsigned)kind < RT_BK_KINDS ? atomic_load(&live[kind]) : 0; }

int rt_d3dback_record_kind(const void *object) {
    const RecObj *o = object;
    return o && o->magic == REC_MAGIC && atomic_load(&o->live) ? (int)o->kind : -1;
}
