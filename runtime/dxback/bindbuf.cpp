// bindbuf.cpp -- the bindbuf group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   VS, PS, GS and CS SetConstantBuffers; VS, PS, GS and CS SetShaderResources
// Only this file fills them; dxback.cpp calls dxback_fill_bindbuf from build_table.
#include "dxback_priv.h"

// ---- the bindbuf group: each entry calls DXMT's method of the same name; the front has made every check ----
// An array of back objects is already an array of DXMT pointers: the array pointer is cast, not copied.
static void b_VSSetConstantBuffers(void *context, UINT start, UINT count, void *const *buffers) {
    static_cast<ID3D11DeviceContext *>(context)->VSSetConstantBuffers(start, count, reinterpret_cast<ID3D11Buffer *const *>(buffers));
}

static void b_PSSetConstantBuffers(void *context, UINT start, UINT count, void *const *buffers) {
    static_cast<ID3D11DeviceContext *>(context)->PSSetConstantBuffers(start, count, reinterpret_cast<ID3D11Buffer *const *>(buffers));
}

static void b_GSSetConstantBuffers(void *context, UINT start, UINT count, void *const *buffers) {
    static_cast<ID3D11DeviceContext *>(context)->GSSetConstantBuffers(start, count, reinterpret_cast<ID3D11Buffer *const *>(buffers));
}

static void b_CSSetConstantBuffers(void *context, UINT start, UINT count, void *const *buffers) {
    static_cast<ID3D11DeviceContext *>(context)->CSSetConstantBuffers(start, count, reinterpret_cast<ID3D11Buffer *const *>(buffers));
}

static void b_VSSetShaderResources(void *context, UINT start, UINT count, void *const *views) {
    static_cast<ID3D11DeviceContext *>(context)->VSSetShaderResources(start, count, reinterpret_cast<ID3D11ShaderResourceView *const *>(views));
}

static void b_PSSetShaderResources(void *context, UINT start, UINT count, void *const *views) {
    static_cast<ID3D11DeviceContext *>(context)->PSSetShaderResources(start, count, reinterpret_cast<ID3D11ShaderResourceView *const *>(views));
}

static void b_GSSetShaderResources(void *context, UINT start, UINT count, void *const *views) {
    static_cast<ID3D11DeviceContext *>(context)->GSSetShaderResources(start, count, reinterpret_cast<ID3D11ShaderResourceView *const *>(views));
}

static void b_CSSetShaderResources(void *context, UINT start, UINT count, void *const *views) {
    static_cast<ID3D11DeviceContext *>(context)->CSSetShaderResources(start, count, reinterpret_cast<ID3D11ShaderResourceView *const *>(views));
}

void dxback_fill_bindbuf(RtD3dBack &t) {
    t.VSSetConstantBuffers = b_VSSetConstantBuffers;
    t.PSSetConstantBuffers = b_PSSetConstantBuffers;
    t.GSSetConstantBuffers = b_GSSetConstantBuffers;
    t.CSSetConstantBuffers = b_CSSetConstantBuffers;
    t.VSSetShaderResources = b_VSSetShaderResources;
    t.PSSetShaderResources = b_PSSetShaderResources;
    t.GSSetShaderResources = b_GSSetShaderResources;
    t.CSSetShaderResources = b_CSSetShaderResources;
}
