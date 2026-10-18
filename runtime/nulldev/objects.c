/* The classes of the do-nothing Direct3D device and what makes their objects (rt/nulldev.h has
 * the model: what each class presents, what its record keeps, who holds a reference to whom). */
#include <string.h>

#include "rt/nulldev.h"

/* A record lets go of what it points at: of a parent, whose count the game sees (a reference),
 * or of an object it kept without the game seeing it (a hold: rt/nulldev.h, REFERENCES). */
static void drop(void *obj)   { if (obj) rt_com_release(obj); }
static void unhold(void *obj) { if (obj) rt_com_unhold(obj); }

/* ---- QueryInterface's documented refusals (rt/com.h, RtComClass.refuses) ------------------------- */

/* A resource that is not a 2D texture, asked for ID3D11Texture2D (the game asks a resource
 * for it at materialsystem_dx11+0x29137, finding 31): it does not implement the interface,
 * and COM's answer for that is E_NOINTERFACE (IUnknown::QueryInterface's page; from memory). */
static bool not_a_texture2d(const void *state, const GUID *iid) {
    (void)state;
    return !memcmp(iid, &rt_iface_ID3D11Texture2D.iid, sizeof *iid);
}

/* A 2D texture asked for IDXGISurface (the game does, at materialsystem_dx11+0x17235).  The
 * IDXGISurface interface's page, from memory: "If the 2D texture has only a single MIP-map
 * level and does not consist of an array of textures, QueryInterface succeeds ... Otherwise,
 * QueryInterface fails and does not return the pointer to IDXGISurface." */
static bool texture2d_refuses(const void *state, const GUID *iid) {
    const NdTexture *t = state;
    return !memcmp(iid, &rt_iface_IDXGISurface.iid, sizeof *iid) && !(t->desc.d2.MipLevels == 1 && t->desc.d2.ArraySize == 1);
}

/* ---- destroy: release what the record holds ------------------------------------------------------- */

static void adapter_destroy(void *state)  { drop(((NdAdapter *)state)->factory); }
static void output_destroy(void *state)   { drop(((NdOutput *)state)->adapter); }
/* The back end's object goes first (rt/d3dback.h, rule 3): its front object is what kept it. */
static void device_destroy(void *state) {
    rt_d3dback_release(RT_BK_DEVICE, &((NdDevice *)state)->back);
    drop(((NdDevice *)state)->adapter);
}
static void child_destroy(void *state) {
    NdChild *c = state;
    rt_d3dback_release((RtD3dBackKind)c->kind, &c->back);
    drop(c->device);
}

static void buffer_destroy(void *state)  { rt_nulldev_memory_free(&((NdBuffer *)state)->mem); child_destroy(state); }
static void texture_destroy(void *state) { rt_nulldev_memory_free(&((NdTexture *)state)->mem); child_destroy(state); }
static void layout_destroy(void *state)  { rt_nulldev_inputlayout_free(state); child_destroy(state); }
static void reflection_destroy(void *state) { rt_nulldev_reflection_free(state); }     /* it belongs to no device */
static void blob_destroy(void *state)       { rt_nulldev_blob_free(state); }

/* A state object takes itself out of its device's list (rt_nulldev_state_create looks there). */
static void blend_destroy(void *state)        { rt_nulldev_state_unlink(state, &rt_nulldev_blend_class); child_destroy(state); }
static void depthstencil_destroy(void *state) { rt_nulldev_state_unlink(state, &rt_nulldev_depthstencil_class); child_destroy(state); }
static void rasterizer_destroy(void *state)   { rt_nulldev_state_unlink(state, &rt_nulldev_rasterizer_class); child_destroy(state); }
static void sampler_destroy(void *state)      { rt_nulldev_state_unlink(state, &rt_nulldev_sampler_class); child_destroy(state); }

static void context_destroy(void *state) {
    rt_nulldev_context_reset(state);
    child_destroy(state);
}

static void view_destroy(void *state) {
    unhold(((NdView *)state)->resource);
    child_destroy(state);
}

static void swapchain_destroy(void *state) {
    NdSwapChain *s = state;
    unhold(s->back_buffer);             /* before the swap chain's own back object: the buffer is the back end's swap chain's */
    unhold(s->factory);                 /* CreateSwapChain binds it (rt_nulldev_bind) */
    rt_d3dback_release(RT_BK_SWAPCHAIN, &s->back);
    drop(s->device);
}

/* ---- the classes ----------------------------------------------------------------------------------
 * The four resource classes present IDXGIResource (unit d3d-fix-2; E8 of
 * docs/reviews/d3d-contract-check.md: Microsoft documents it on every Direct3D 11 resource, and
 * Wine's test_dxgi_resource finds IDXGIDeviceSubObject and IDXGIObject answered with that same
 * pointer).  A query for one of those two bases is answered by the first face that has it, so
 * on a 2D texture IDXGIResource stands before IDXGISurface.  Its four methods have no body:
 * the recording never calls one (finding 40), and a stop names the method. */

#define CLASS(var, text, record, destroy_fn, refuses_fn, ...) \
    static RtComIface *const var##_faces[] = { __VA_ARGS__ }; \
    const RtComClass rt_nulldev_##var##_class = { text, var##_faces, sizeof var##_faces / sizeof var##_faces[0], \
                                                  record, destroy_fn, refuses_fn }

CLASS(factory,      "DXGIFactory",             0,                     NULL,              NULL, &rt_iface_IDXGIFactory1);
CLASS(adapter,      "DXGIAdapter",             sizeof(NdAdapter),     adapter_destroy,   NULL, &rt_iface_IDXGIAdapter1);
CLASS(output,       "DXGIOutput",              sizeof(NdOutput),      output_destroy,    NULL, &rt_iface_IDXGIOutput);
CLASS(device,       "D3D11Device",             sizeof(NdDevice),      device_destroy,    NULL, &rt_iface_ID3D11Device, &rt_iface_IDXGIDevice1);
CLASS(context,      "D3D11DeviceContext",      sizeof(NdContext),     context_destroy,   NULL, &rt_iface_ID3D11DeviceContext);
CLASS(swapchain,    "DXGISwapChain",           sizeof(NdSwapChain),   swapchain_destroy, NULL, &rt_iface_IDXGISwapChain);
CLASS(buffer,       "D3D11Buffer",             sizeof(NdBuffer),      buffer_destroy,    not_a_texture2d, &rt_iface_ID3D11Buffer, &rt_iface_IDXGIResource);
CLASS(texture1d,    "D3D11Texture1D",          sizeof(NdTexture),     texture_destroy,   not_a_texture2d, &rt_iface_ID3D11Texture1D, &rt_iface_IDXGIResource);
CLASS(texture2d,    "D3D11Texture2D",          sizeof(NdTexture),     texture_destroy,   texture2d_refuses, &rt_iface_ID3D11Texture2D, &rt_iface_IDXGIResource,
      &rt_iface_IDXGISurface);
CLASS(texture3d,    "D3D11Texture3D",          sizeof(NdTexture),     texture_destroy,   not_a_texture2d, &rt_iface_ID3D11Texture3D, &rt_iface_IDXGIResource);
CLASS(srv,          "D3D11ShaderResourceView", sizeof(NdView),        view_destroy,      NULL, &rt_iface_ID3D11ShaderResourceView);
CLASS(uav,          "D3D11UnorderedAccessView", sizeof(NdView),       view_destroy,      NULL, &rt_iface_ID3D11UnorderedAccessView);
CLASS(rtv,          "D3D11RenderTargetView",   sizeof(NdView),        view_destroy,      NULL, &rt_iface_ID3D11RenderTargetView);
CLASS(dsv,          "D3D11DepthStencilView",   sizeof(NdView),        view_destroy,      NULL, &rt_iface_ID3D11DepthStencilView);
CLASS(blend,        "D3D11BlendState",         sizeof(NdStateObject), blend_destroy,     NULL, &rt_iface_ID3D11BlendState);
CLASS(depthstencil, "D3D11DepthStencilState",  sizeof(NdStateObject), depthstencil_destroy, NULL, &rt_iface_ID3D11DepthStencilState);
CLASS(rasterizer,   "D3D11RasterizerState",    sizeof(NdStateObject), rasterizer_destroy, NULL, &rt_iface_ID3D11RasterizerState);
CLASS(sampler,      "D3D11SamplerState",       sizeof(NdStateObject), sampler_destroy,   NULL, &rt_iface_ID3D11SamplerState);
CLASS(vs,           "D3D11VertexShader",       sizeof(NdShader),      child_destroy,     NULL, &rt_iface_ID3D11VertexShader);
CLASS(ps,           "D3D11PixelShader",        sizeof(NdShader),      child_destroy,     NULL, &rt_iface_ID3D11PixelShader);
CLASS(gs,           "D3D11GeometryShader",     sizeof(NdShader),      child_destroy,     NULL, &rt_iface_ID3D11GeometryShader);
CLASS(cs,           "D3D11ComputeShader",      sizeof(NdShader),      child_destroy,     NULL, &rt_iface_ID3D11ComputeShader);
CLASS(inputlayout,  "D3D11InputLayout",        sizeof(NdInputLayout), layout_destroy,    NULL, &rt_iface_ID3D11InputLayout);
CLASS(query,        "D3D11Query",              sizeof(NdQuery),       child_destroy,     NULL, &rt_iface_ID3D11Query);
CLASS(reflection,   "D3D11ShaderReflection",   sizeof(NdReflection),  reflection_destroy, NULL, &rt_iface_ID3D11ShaderReflection);
CLASS(blob,         "D3DBlob",                 sizeof(NdBlob),        blob_destroy,      NULL, &rt_iface_ID3D10Blob);

/* The classes whose record begins with the device (NdChild, and NdSwapChain's first field). */
static const RtComClass *const of_a_device[] = {
    &rt_nulldev_context_class, &rt_nulldev_swapchain_class, &rt_nulldev_buffer_class, &rt_nulldev_texture1d_class,
    &rt_nulldev_texture2d_class, &rt_nulldev_texture3d_class, &rt_nulldev_srv_class, &rt_nulldev_uav_class,
    &rt_nulldev_rtv_class, &rt_nulldev_dsv_class, &rt_nulldev_blend_class, &rt_nulldev_depthstencil_class,
    &rt_nulldev_rasterizer_class, &rt_nulldev_sampler_class, &rt_nulldev_vs_class, &rt_nulldev_ps_class,
    &rt_nulldev_gs_class, &rt_nulldev_cs_class, &rt_nulldev_inputlayout_class, &rt_nulldev_query_class,
};
_Static_assert(offsetof(NdSwapChain, device) == 0 && offsetof(NdContext, child) == 0 && offsetof(NdBuffer, child) == 0 &&
               offsetof(NdTexture, child) == 0 && offsetof(NdView, child) == 0 && offsetof(NdStateObject, child) == 0 &&
               offsetof(NdShader, child) == 0 && offsetof(NdInputLayout, child) == 0 && offsetof(NdQuery, child) == 0,
               "rt_nulldev_new writes the device at offset 0 of the record");
_Static_assert(offsetof(NdSwapChain, back) == offsetof(NdChild, back) && offsetof(NdSwapChain, kind) == offsetof(NdChild, kind),
               "a swap chain's record begins as NdChild does: rt_nulldev_new and rt_d3dback_of read it so");

/* The kind of each class that has a back object, in RtD3dBackKind's order (the device is not
 * of_a_device's: its record is NdDevice). */
static const RtComClass *const back_kinds[RT_BK_KINDS] = {
    [RT_BK_DEVICE] = &rt_nulldev_device_class, [RT_BK_CONTEXT] = &rt_nulldev_context_class, [RT_BK_SWAPCHAIN] = &rt_nulldev_swapchain_class,
    [RT_BK_BUFFER] = &rt_nulldev_buffer_class, [RT_BK_TEXTURE1D] = &rt_nulldev_texture1d_class, [RT_BK_TEXTURE2D] = &rt_nulldev_texture2d_class,
    [RT_BK_TEXTURE3D] = &rt_nulldev_texture3d_class, [RT_BK_SRV] = &rt_nulldev_srv_class, [RT_BK_UAV] = &rt_nulldev_uav_class,
    [RT_BK_RTV] = &rt_nulldev_rtv_class, [RT_BK_DSV] = &rt_nulldev_dsv_class, [RT_BK_BLEND] = &rt_nulldev_blend_class,
    [RT_BK_DEPTHSTENCIL] = &rt_nulldev_depthstencil_class, [RT_BK_RASTERIZER] = &rt_nulldev_rasterizer_class,
    [RT_BK_SAMPLER] = &rt_nulldev_sampler_class, [RT_BK_VS] = &rt_nulldev_vs_class, [RT_BK_PS] = &rt_nulldev_ps_class,
    [RT_BK_GS] = &rt_nulldev_gs_class, [RT_BK_CS] = &rt_nulldev_cs_class, [RT_BK_INPUTLAYOUT] = &rt_nulldev_inputlayout_class,
    [RT_BK_QUERY] = &rt_nulldev_query_class,
};

int rt_nulldev_back_kind(const RtComClass *cls) {
    for (int k = 0; k < RT_BK_KINDS; k++)
        if (back_kinds[k] == cls) return k;
    return -1;
}

/* ---- making objects ------------------------------------------------------------------------------- */

IDXGIAdapter1 *rt_nulldev_adapter_new(IDXGIFactory1 *factory) {
    (void)rt_com_state_of(factory, &rt_nulldev_factory_class);          /* fatal unless it is a factory */
    IDXGIAdapter1 *adapter = rt_com_new(&rt_nulldev_adapter_class);
    NdAdapter *a = rt_com_state_of(adapter, &rt_nulldev_adapter_class);
    rt_com_addref(factory);
    a->factory = factory;
    return adapter;
}

IDXGIOutput *rt_nulldev_output_new(IDXGIAdapter1 *adapter) {
    (void)rt_com_state_of(adapter, &rt_nulldev_adapter_class);
    IDXGIOutput *output = rt_com_new(&rt_nulldev_output_class);
    NdOutput *o = rt_com_state_of(output, &rt_nulldev_output_class);
    rt_com_addref(adapter);
    o->adapter = adapter;
    return output;
}

void *rt_nulldev_new(ID3D11Device *device, const RtComClass *cls) {
    (void)rt_com_state_of(device, &rt_nulldev_device_class);
    unsigned i = 0;
    while (i < sizeof of_a_device / sizeof of_a_device[0] && of_a_device[i] != cls) i++;
    if (i == sizeof of_a_device / sizeof of_a_device[0])
        rt_fatal("rt_nulldev_new: class %s is not one a device makes objects of", cls ? cls->name : "(null)");
    void *obj = rt_com_new(cls);
    NdChild *c = rt_com_state_of(obj, cls);
    rt_com_addref(device);
    c->device = device;
    c->kind = (uint32_t)rt_nulldev_back_kind(cls);      /* every class of of_a_device has one */
    if (cls == &rt_nulldev_context_class) rt_nulldev_context_reset((NdContext *)c);
    return obj;
}

void rt_nulldev_create(ID3D11Device **device, ID3D11DeviceContext **context) {
    rt_d3dback_select();                                /* RT_D3D_BACKEND, read once: before the first object */
    IDXGIFactory1 *factory = rt_com_new(&rt_nulldev_factory_class);
    ID3D11Device *dev = rt_com_new(&rt_nulldev_device_class);
    NdDevice *d = rt_com_state_of(dev, &rt_nulldev_device_class);
    d->adapter = rt_nulldev_adapter_new(factory);       /* the device's reference */
    rt_com_release(factory);                            /* the adapter holds the factory now */
    d->level = ND_FEATURE_LEVEL;
    d->max_frame_latency = 3;
    *device = dev;
    *context = rt_nulldev_new(dev, &rt_nulldev_context_class);
    /* The back end's device and context, when there is one (rt/d3dback.h, rules 3 and 4).  No
     * guest call is in progress for a test that makes a device, so a refusal is fatal here. */
    const RtD3dBack *back = rt_d3dback();
    if (back) {
        void *bd = NULL, *bc = NULL;
        rt_d3dback_reach(NULL, RT_BACK_CreateDevice);  /* counted as asked; no guest call is in progress */
        HRESULT hr = back->CreateDevice(&bd, &bc);
        if (hr != S_OK || !bd || !bc)
            rt_fatal("rt_nulldev_create: the back end (%s) answered 0x%08x to CreateDevice (device %p, context %p), where the "
                     "front made a device", back->name, (unsigned)hr, bd, bc);
        rt_d3dback_attach(dev, bd);
        rt_d3dback_attach(*context, bc);
    }
}

/* ---- capabilities ---------------------------------------------------------------------------------- */

/* What the native renderer answered on this Mac to ID3D11Device::CheckFormatSupport for every
 * format number from 1 to 132 (rt/nulldev.h): THE WHOLE COLUMN OF tools/dxmt/format_probe.out,
 * written out by number (the planner's answer C to gate3-run-4; the probe was not run again).
 * how: 0 not measured (format 0), 1 the device answered S_OK with these bits, 2 it answered
 * E_INVALIDARG (0x80070057) and wrote 0: 98 and 34 of the 132. */
static const struct { uint8_t how; UINT support; } format_measured[133] = {
    /*   0 */ { 0, 0 }, { 1, 0x02d273f1 }, { 1, 0x02f6f3fb }, { 1, 0x02d273fb }, { 1, 0x02d273fb }, { 1, 0x02d273f5 },
    /*   6 */ { 1, 0x02f6f3fb }, { 1, 0x02d273ff }, { 1, 0x02d273fb }, { 1, 0x02f6f3f1 }, { 1, 0x02fef3f3 }, { 1, 0x02f2f3f3 },
    /*  12 */ { 1, 0x02f273f3 }, { 1, 0x02f2f3f3 }, { 1, 0x02f273f3 }, { 1, 0x02f273f1 }, { 1, 0x02f6f3fb }, { 1, 0x02f273fb },
    /*  18 */ { 1, 0x02f273fb }, { 1, 0x04f717f0 }, { 1, 0x04f717f0 }, { 1, 0x04f717f0 }, { 1, 0x04f317f0 }, { 1, 0x02f273f1 },
    /*  24 */ { 1, 0x02fef3f3 }, { 1, 0x02f273f1 }, { 1, 0x02f6f3f3 }, { 1, 0x02f6f3f1 }, { 1, 0x02fef3f3 }, { 1, 0x02fef3f1 },
    /*  30 */ { 1, 0x02f273f3 }, { 1, 0x02f6f3f3 }, { 1, 0x02f273f3 }, { 1, 0x02f273f1 }, { 1, 0x02f6f3f3 }, { 1, 0x02f2f3f3 },
    /*  36 */ { 1, 0x02f273f3 }, { 1, 0x02f2f3f3 }, { 1, 0x02f273f3 }, { 1, 0x02d273f5 }, { 1, 0x04f717f0 }, { 1, 0x02f6f3fb },
    /*  42 */ { 1, 0x02d273ff }, { 1, 0x02d273fb }, { 1, 0x04f717f0 }, { 1, 0x04f717f0 }, { 1, 0x04f717f0 }, { 1, 0x04f317f0 },
    /*  48 */ { 1, 0x02f273f1 }, { 1, 0x02f6f3f3 }, { 1, 0x02f273f3 }, { 1, 0x02f6f3f3 }, { 1, 0x02f273f3 }, { 1, 0x02f273f5 },
    /*  54 */ { 1, 0x02f6f3f3 }, { 1, 0x04f717f0 }, { 1, 0x02f2f3f3 }, { 1, 0x02f273f7 }, { 1, 0x02f2f3f3 }, { 1, 0x02f273f3 },
    /*  60 */ { 1, 0x02f273f1 }, { 1, 0x02f6f3f3 }, { 1, 0x02f273f3 }, { 1, 0x02f6f3f3 }, { 1, 0x02f273f3 }, { 1, 0x02f6f3f1 },
    /*  66 */ { 2, 0 }, { 1, 0x02f6f3f3 }, { 1, 0x00d233f1 }, { 1, 0x00d233f1 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 },
    /*  72 */ { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 },
    /*  78 */ { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 },
    /*  84 */ { 1, 0x00d213f0 }, { 1, 0x02f2f3f1 }, { 1, 0x02f2f3f1 }, { 1, 0x02fef3f3 }, { 1, 0x02f6f3f1 }, { 2, 0 },
    /*  90 */ { 1, 0x02f6f3f1 }, { 1, 0x02fef3f1 }, { 1, 0x02f6f3f1 }, { 1, 0x02f6f3f1 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 },
    /*  96 */ { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 1, 0x00d213f0 }, { 2, 0 }, { 2, 0 },
    /* 102 */ { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 },
    /* 108 */ { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 },
    /* 114 */ { 2, 0 }, { 1, 0x02f2f3f1 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 },
    /* 120 */ { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 },
    /* 126 */ { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 }, { 2, 0 },
    /* 132 */ { 2, 0 },
};

bool rt_nulldev_format_measured(DXGI_FORMAT format, HRESULT *hr, UINT *support) {
    if ((unsigned)format >= sizeof format_measured / sizeof format_measured[0] || !format_measured[format].how) return false;
    *hr = format_measured[format].how == 1 ? S_OK : E_INVALIDARG;
    *support = format_measured[format].support;
    return true;
}

bool rt_nulldev_format_support(DXGI_FORMAT format, UINT *support) {
    HRESULT hr;
    return rt_nulldev_format_measured(format, &hr, support) && hr == S_OK;
}

/* ---- helpers for method bodies ------------------------------------------------------------------- */

static _Noreturn void wrong_arg(Ctx *ctx, void *obj, const RtComClass *is, const char *what, const char *wanted) {
    if (is) RT_UNIMPLEMENTED(ctx, "%s (%p) is an object of class %s, where %s is expected", what, obj, is->name, wanted);
    RT_UNIMPLEMENTED(ctx, "%s (%p) is not a live object of the interface layer, where %s is expected", what, obj, wanted);
}

void *rt_nulldev_arg(Ctx *ctx, void *obj, const RtComClass *cls, const char *what) {
    if (!obj) return NULL;
    const RtComClass *is = rt_com_class_of(obj);
    if (is != cls) wrong_arg(ctx, obj, is, what, cls->name);
    return obj;
}

void *rt_nulldev_resource_arg(Ctx *ctx, void *obj, const char *what) {
    if (!obj) return NULL;
    const RtComClass *is = rt_com_class_of(obj);
    if (is != &rt_nulldev_buffer_class && is != &rt_nulldev_texture1d_class && is != &rt_nulldev_texture2d_class &&
        is != &rt_nulldev_texture3d_class)
        wrong_arg(ctx, obj, is, what, "a buffer or a texture");
    return obj;
}

HRESULT rt_nulldev_get_parent(Ctx *ctx, void *parent, REFIID riid, void **out) {
    if (!riid || !out) RT_UNIMPLEMENTED(ctx, "GetParent with riid %p and out-pointer %p", (const void *)riid, (void *)out);
    const RtComIface *want = rt_com_iface_by_iid(riid);
    void *face = want ? rt_com_face(parent, want) : NULL;
    if (!face)
        RT_UNIMPLEMENTED(ctx, "GetParent for {%08x-%04x-%04x-%02x%02x-...} (%s), which the parent, an object of class %s, "
                         "does not present", (unsigned)riid->Data1, (unsigned)riid->Data2, (unsigned)riid->Data3,
                         riid->Data4[0], riid->Data4[1], want ? want->name : "an interface the runtime does not describe",
                         rt_com_class_of(parent)->name);
    rt_com_addref(face);
    *out = face;
    return S_OK;
}

void rt_nulldev_bind(void *slot, void *obj) {
    void **s = slot, *old = *s;
    if (obj) rt_com_hold(obj);
    *s = obj;
    unhold(old);
}

void *rt_nulldev_ref(void *obj) {
    if (obj) rt_com_addref(obj);
    return obj;
}

void rt_nulldev_context_reset(NdContext *c) {
    for (unsigned i = 0; i < ND_CB_SLOTS; i++) { unhold(c->vs_cb[i]); unhold(c->ps_cb[i]); }
    for (unsigned i = 0; i < ND_SRV_SLOTS; i++) unhold(c->ps_srv[i]);
    for (unsigned i = 0; i < ND_SAMPLER_SLOTS; i++) unhold(c->ps_sampler[i]);
    for (unsigned i = 0; i < ND_RT_SLOTS; i++) unhold(c->rtv[i]);
    unhold(c->vs); unhold(c->ps); unhold(c->gs); unhold(c->hs); unhold(c->ds);
    unhold(c->input_layout); unhold(c->dsv); unhold(c->blend); unhold(c->depth_stencil); unhold(c->rasterizer);
    NdChild child = c->child;           /* the device, the back end's context, the kind: not state */
    memset(c, 0, sizeof *c);
    c->child = child;
    for (unsigned i = 0; i < 4; i++) c->blend_factor[i] = 1.0f;
    c->sample_mask = D3D11_DEFAULT_SAMPLE_MASK;
}
