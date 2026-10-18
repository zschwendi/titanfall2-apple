// states.cpp -- the states group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   CreateBlendState, CreateDepthStencilState, CreateRasterizerState, CreateSamplerState, CreateQuery
// Only this file fills them; dxback.cpp calls dxback_fill_states from build_table.
#include "dxback_priv.h"

// ---- the state objects and the query: each casts the back objects, calls DXMT's method, returns its result ----
static HRESULT b_CreateBlendState(void *device, const D3D11_BLEND_DESC *desc, void **state) {
    ID3D11BlendState *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateBlendState(desc, &s);
    *state = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

static HRESULT b_CreateDepthStencilState(void *device, const D3D11_DEPTH_STENCIL_DESC *desc, void **state) {
    ID3D11DepthStencilState *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateDepthStencilState(desc, &s);
    *state = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

static HRESULT b_CreateRasterizerState(void *device, const D3D11_RASTERIZER_DESC *desc, void **state) {
    ID3D11RasterizerState *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateRasterizerState(desc, &s);
    *state = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

static HRESULT b_CreateSamplerState(void *device, const D3D11_SAMPLER_DESC *desc, void **state) {
    ID3D11SamplerState *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateSamplerState(desc, &s);
    *state = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

static HRESULT b_CreateQuery(void *device, const D3D11_QUERY_DESC *desc, void **query) {
    ID3D11Query *q = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateQuery(desc, &q);
    *query = q ? static_cast<void *>(q) : nullptr;
    return hr;
}

void dxback_fill_states(RtD3dBack &t) {
    t.CreateBlendState = b_CreateBlendState;
    t.CreateDepthStencilState = b_CreateDepthStencilState;
    t.CreateRasterizerState = b_CreateRasterizerState;
    t.CreateSamplerState = b_CreateSamplerState;
    t.CreateQuery = b_CreateQuery;
}
