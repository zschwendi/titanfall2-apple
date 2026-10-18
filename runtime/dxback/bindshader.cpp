// bindshader.cpp -- the bindshader group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   VS, PS, GS and CS SetSamplers; VS, PS, GS, CS, HS and DS SetShader
// Only this file fills them; dxback.cpp calls dxback_fill_bindshader from build_table.
#include "dxback_priv.h"

// ---- the sampler binds: an array of back objects is already an array of DXMT pointers ----
static void b_VSSetSamplers(void *context, UINT start, UINT count, void *const *samplers) {
    static_cast<ID3D11DeviceContext *>(context)->VSSetSamplers(start, count, reinterpret_cast<ID3D11SamplerState *const *>(samplers));
}

static void b_PSSetSamplers(void *context, UINT start, UINT count, void *const *samplers) {
    static_cast<ID3D11DeviceContext *>(context)->PSSetSamplers(start, count, reinterpret_cast<ID3D11SamplerState *const *>(samplers));
}

static void b_GSSetSamplers(void *context, UINT start, UINT count, void *const *samplers) {
    static_cast<ID3D11DeviceContext *>(context)->GSSetSamplers(start, count, reinterpret_cast<ID3D11SamplerState *const *>(samplers));
}

static void b_CSSetSamplers(void *context, UINT start, UINT count, void *const *samplers) {
    static_cast<ID3D11DeviceContext *>(context)->CSSetSamplers(start, count, reinterpret_cast<ID3D11SamplerState *const *>(samplers));
}

// ---- the shader binds: a NULL shader is valid; class instances do not cross (the front stops on any), so none are passed ----
static void b_VSSetShader(void *context, void *shader) {
    static_cast<ID3D11DeviceContext *>(context)->VSSetShader(static_cast<ID3D11VertexShader *>(shader), nullptr, 0);
}

static void b_PSSetShader(void *context, void *shader) {
    static_cast<ID3D11DeviceContext *>(context)->PSSetShader(static_cast<ID3D11PixelShader *>(shader), nullptr, 0);
}

static void b_GSSetShader(void *context, void *shader) {
    static_cast<ID3D11DeviceContext *>(context)->GSSetShader(static_cast<ID3D11GeometryShader *>(shader), nullptr, 0);
}

static void b_CSSetShader(void *context, void *shader) {
    static_cast<ID3D11DeviceContext *>(context)->CSSetShader(static_cast<ID3D11ComputeShader *>(shader), nullptr, 0);
}

static void b_HSSetShader(void *context, void *shader) {
    static_cast<ID3D11DeviceContext *>(context)->HSSetShader(static_cast<ID3D11HullShader *>(shader), nullptr, 0);
}

static void b_DSSetShader(void *context, void *shader) {
    static_cast<ID3D11DeviceContext *>(context)->DSSetShader(static_cast<ID3D11DomainShader *>(shader), nullptr, 0);
}

void dxback_fill_bindshader(RtD3dBack &t) {
    t.VSSetSamplers = b_VSSetSamplers;
    t.PSSetSamplers = b_PSSetSamplers;
    t.GSSetSamplers = b_GSSetSamplers;
    t.CSSetSamplers = b_CSSetSamplers;
    t.VSSetShader = b_VSSetShader;
    t.PSSetShader = b_PSSetShader;
    t.GSSetShader = b_GSSetShader;
    t.CSSetShader = b_CSSetShader;
    t.HSSetShader = b_HSSetShader;
    t.DSSetShader = b_DSSetShader;
}
