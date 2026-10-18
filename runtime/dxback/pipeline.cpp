// pipeline.cpp -- the pipeline group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   IASetVertexBuffers, IASetInputLayout, IASetIndexBuffer, IASetPrimitiveTopology, RSSetState, RSSetViewports, OMSetBlendState, OMSetDepthStencilState, OMSetRenderTargets, OMSetRenderTargetsAndUnorderedAccessViews, CSSetUnorderedAccessViews
// Only this file fills them; dxback.cpp calls dxback_fill_pipeline from build_table.
#include "dxback_priv.h"

// ---- the input assembler: each casts the back objects, calls DXMT's method of the same name ----
static void b_IASetVertexBuffers(void *context, UINT start, UINT count, void *const *buffers, const UINT *strides, const UINT *offsets) {
    static_cast<ID3D11DeviceContext *>(context)->IASetVertexBuffers(start, count,
        reinterpret_cast<ID3D11Buffer *const *>(buffers), strides, offsets);
}

static void b_IASetInputLayout(void *context, void *layout) {
    static_cast<ID3D11DeviceContext *>(context)->IASetInputLayout(static_cast<ID3D11InputLayout *>(layout));
}

static void b_IASetIndexBuffer(void *context, void *buffer, DXGI_FORMAT format, UINT offset) {
    static_cast<ID3D11DeviceContext *>(context)->IASetIndexBuffer(static_cast<ID3D11Buffer *>(buffer), format, offset);
}

static void b_IASetPrimitiveTopology(void *context, D3D11_PRIMITIVE_TOPOLOGY topology) {
    static_cast<ID3D11DeviceContext *>(context)->IASetPrimitiveTopology(topology);
}

// ---- the rasterizer and the viewports ----
static void b_RSSetState(void *context, void *state) {
    static_cast<ID3D11DeviceContext *>(context)->RSSetState(static_cast<ID3D11RasterizerState *>(state));
}

static void b_RSSetViewports(void *context, UINT count, const D3D11_VIEWPORT *viewports) {
    static_cast<ID3D11DeviceContext *>(context)->RSSetViewports(count, viewports);
}

// ---- the output merger: the states, the render targets and the unordered-access views ----
static void b_OMSetBlendState(void *context, void *state, const FLOAT *factor, UINT sample_mask) {
    static_cast<ID3D11DeviceContext *>(context)->OMSetBlendState(static_cast<ID3D11BlendState *>(state), factor, sample_mask);
}

static void b_OMSetDepthStencilState(void *context, void *state, UINT stencil_ref) {
    static_cast<ID3D11DeviceContext *>(context)->OMSetDepthStencilState(static_cast<ID3D11DepthStencilState *>(state), stencil_ref);
}

static void b_OMSetRenderTargets(void *context, UINT count, void *const *views, void *depth_stencil_view) {
    static_cast<ID3D11DeviceContext *>(context)->OMSetRenderTargets(count,
        reinterpret_cast<ID3D11RenderTargetView *const *>(views), static_cast<ID3D11DepthStencilView *>(depth_stencil_view));
}

static void b_OMSetRenderTargetsAndUnorderedAccessViews(void *context, UINT rtv_count, void *const *rtvs, void *depth_stencil_view,
                                                        UINT uav_start, UINT uav_count, void *const *uavs, const UINT *initial_counts) {
    static_cast<ID3D11DeviceContext *>(context)->OMSetRenderTargetsAndUnorderedAccessViews(rtv_count,
        reinterpret_cast<ID3D11RenderTargetView *const *>(rtvs), static_cast<ID3D11DepthStencilView *>(depth_stencil_view),
        uav_start, uav_count, reinterpret_cast<ID3D11UnorderedAccessView *const *>(uavs), initial_counts);
}

// ---- the compute stage's unordered-access views ----
static void b_CSSetUnorderedAccessViews(void *context, UINT start, UINT count, void *const *views, const UINT *initial_counts) {
    static_cast<ID3D11DeviceContext *>(context)->CSSetUnorderedAccessViews(start, count,
        reinterpret_cast<ID3D11UnorderedAccessView *const *>(views), initial_counts);
}

void dxback_fill_pipeline(RtD3dBack &t) {
    t.IASetVertexBuffers = b_IASetVertexBuffers;
    t.IASetInputLayout = b_IASetInputLayout;
    t.IASetIndexBuffer = b_IASetIndexBuffer;
    t.IASetPrimitiveTopology = b_IASetPrimitiveTopology;
    t.RSSetState = b_RSSetState;
    t.RSSetViewports = b_RSSetViewports;
    t.OMSetBlendState = b_OMSetBlendState;
    t.OMSetDepthStencilState = b_OMSetDepthStencilState;
    t.OMSetRenderTargets = b_OMSetRenderTargets;
    t.OMSetRenderTargetsAndUnorderedAccessViews = b_OMSetRenderTargetsAndUnorderedAccessViews;
    t.CSSetUnorderedAccessViews = b_CSSetUnorderedAccessViews;
}
