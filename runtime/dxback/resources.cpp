// resources.cpp -- the resources group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   CreateTexture1D, CreateTexture2D, CreateTexture3D, CreateShaderResourceView, CreateUnorderedAccessView, CreateRenderTargetView, CreateDepthStencilView
// Only this file fills them; dxback.cpp calls dxback_fill_resources from build_table.
#include "dxback_priv.h"

// ---- the resources group: each entry calls DXMT's method of the same name; the front has made every check ----
static HRESULT b_CreateTexture1D(void *device, const D3D11_TEXTURE1D_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **texture) {
    ID3D11Texture1D *t = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateTexture1D(desc, initial, &t);
    *texture = t ? static_cast<void *>(t) : nullptr;
    return hr;
}

static HRESULT b_CreateTexture2D(void *device, const D3D11_TEXTURE2D_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **texture) {
    ID3D11Texture2D *t = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateTexture2D(desc, initial, &t);
    *texture = t ? static_cast<void *>(t) : nullptr;
    return hr;
}

static HRESULT b_CreateTexture3D(void *device, const D3D11_TEXTURE3D_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **texture) {
    ID3D11Texture3D *t = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateTexture3D(desc, initial, &t);
    *texture = t ? static_cast<void *>(t) : nullptr;
    return hr;
}

static HRESULT b_CreateShaderResourceView(void *device, void *resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, void **view) {
    ID3D11ShaderResourceView *v = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateShaderResourceView(static_cast<ID3D11Resource *>(resource), desc, &v);
    *view = v ? static_cast<void *>(v) : nullptr;
    return hr;
}

static HRESULT b_CreateUnorderedAccessView(void *device, void *resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc, void **view) {
    ID3D11UnorderedAccessView *v = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateUnorderedAccessView(static_cast<ID3D11Resource *>(resource), desc, &v);
    *view = v ? static_cast<void *>(v) : nullptr;
    return hr;
}

static HRESULT b_CreateRenderTargetView(void *device, void *resource, const D3D11_RENDER_TARGET_VIEW_DESC *desc, void **view) {
    ID3D11RenderTargetView *v = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateRenderTargetView(static_cast<ID3D11Resource *>(resource), desc, &v);
    *view = v ? static_cast<void *>(v) : nullptr;
    return hr;
}

static HRESULT b_CreateDepthStencilView(void *device, void *resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *desc, void **view) {
    ID3D11DepthStencilView *v = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateDepthStencilView(static_cast<ID3D11Resource *>(resource), desc, &v);
    *view = v ? static_cast<void *>(v) : nullptr;
    return hr;
}

void dxback_fill_resources(RtD3dBack &t) {
    t.CreateTexture1D = b_CreateTexture1D;
    t.CreateTexture2D = b_CreateTexture2D;
    t.CreateTexture3D = b_CreateTexture3D;
    t.CreateShaderResourceView = b_CreateShaderResourceView;
    t.CreateUnorderedAccessView = b_CreateUnorderedAccessView;
    t.CreateRenderTargetView = b_CreateRenderTargetView;
    t.CreateDepthStencilView = b_CreateDepthStencilView;
}
