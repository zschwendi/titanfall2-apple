// shaders.cpp -- the shaders group of the DXMT back end's table (unit dxback-split).  Entries that belong here:
//   CreateInputLayout, CreateVertexShader, CreateGeometryShader, CreatePixelShader, CreateComputeShader
// Only this file fills them; dxback.cpp calls dxback_fill_shaders from build_table.
//
// Each entry casts the back objects to DXMT's interfaces and calls DXMT's method of the same name.  DXMT's shader
// creators take a class linkage as well; the seam has none, and the front stops on any class linkage that is not
// NULL (runtime/nulldev/creators.c, RT_UNIMPLEMENTED), so the back end always gets NULL there.
#include "dxback_priv.h"

static HRESULT b_CreateInputLayout(void *device, const D3D11_INPUT_ELEMENT_DESC *elements, UINT count,
                                   const void *bytecode, SIZE_T length, void **layout) {
    ID3D11InputLayout *l = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateInputLayout(elements, count, bytecode, length, &l);
    *layout = l ? static_cast<void *>(l) : nullptr;
    return hr;
}

static HRESULT b_CreateVertexShader(void *device, const void *bytecode, SIZE_T length, void **shader) {
    ID3D11VertexShader *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateVertexShader(bytecode, length, nullptr, &s);
    *shader = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

static HRESULT b_CreateGeometryShader(void *device, const void *bytecode, SIZE_T length, void **shader) {
    ID3D11GeometryShader *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateGeometryShader(bytecode, length, nullptr, &s);
    *shader = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

static HRESULT b_CreatePixelShader(void *device, const void *bytecode, SIZE_T length, void **shader) {
    ID3D11PixelShader *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreatePixelShader(bytecode, length, nullptr, &s);
    *shader = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

static HRESULT b_CreateComputeShader(void *device, const void *bytecode, SIZE_T length, void **shader) {
    ID3D11ComputeShader *s = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateComputeShader(bytecode, length, nullptr, &s);
    *shader = s ? static_cast<void *>(s) : nullptr;
    return hr;
}

void dxback_fill_shaders(RtD3dBack &t) {
    t.CreateInputLayout = b_CreateInputLayout;
    t.CreateVertexShader = b_CreateVertexShader;
    t.CreateGeometryShader = b_CreateGeometryShader;
    t.CreatePixelShader = b_CreatePixelShader;
    t.CreateComputeShader = b_CreateComputeShader;
}
