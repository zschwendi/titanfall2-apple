/* Direct3D 11 and DXGI: the constants and structures the runtime's own code uses (d3dcommon.h,
 * d3d11.h, dxgi.h).  Planner-owned.
 *
 * The TYPE NAMES (D3D_FEATURE_LEVEL, D3D11_BUFFER_DESC, ID3D11Device, ...) are not declared
 * here: they come from the interface descriptions, as build/gen/com_types.h (enumerations as
 * uint32_t, structures and interfaces as incomplete structs; runtime/com/types.def).  This
 * header adds what a description cannot give: enumerator values, and, when a method body
 * needs to read or fill a structure, its layout, written as
 *     struct D3D11_BUFFER_DESC { UINT ByteWidth; ... };  _Static_assert(sizeof(...) == ..., "");
 * which completes the incomplete struct of the same name.  Rules of docs/RUNTIME.md 3.3: fixed
 * widths, no bit-fields, an assert on the size and on every offset used.
 *
 * Every value says where it comes from.  "MinGW" is the MinGW-w64 header of that name in
 * third_party/dxmt/include/native/directx (the source of the descriptions too).
 */
#ifndef RT_WIN_D3D11_H
#define RT_WIN_D3D11_H

#include <stddef.h>

#include "rt/wintypes.h"
#include "rt/win/dxgi.h"        /* DXGI_FORMAT's enumerators, DXGI_SAMPLE_DESC, the DXGI structures */

/* D3D_DRIVER_TYPE (MinGW d3dcommon.h). */
#define D3D_DRIVER_TYPE_UNKNOWN    0u
#define D3D_DRIVER_TYPE_HARDWARE   1u

/* D3D_FEATURE_LEVEL (MinGW d3dcommon.h). */
#define D3D_FEATURE_LEVEL_11_0     0xb000u

/* The SDKVersion argument of D3D11CreateDevice (MinGW d3d11.h). */
#define D3D11_SDK_VERSION          7u

/* Direct3D 11 structures (MinGW d3d11.h; D3D11_SIGNATURE_PARAMETER_DESC from d3d11shader.h).
 * An enumeration field is written as UINT, with its enumeration named in a comment. */
struct D3D11_BUFFER_DESC {
    UINT ByteWidth;
    UINT Usage;                  /* D3D11_USAGE */
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
};
typedef struct D3D11_BUFFER_DESC D3D11_BUFFER_DESC;
_Static_assert(sizeof(struct D3D11_BUFFER_DESC) == 24, "");
_Static_assert(offsetof(struct D3D11_BUFFER_DESC, ByteWidth) == 0, "");
_Static_assert(offsetof(struct D3D11_BUFFER_DESC, Usage) == 4, "");
_Static_assert(offsetof(struct D3D11_BUFFER_DESC, BindFlags) == 8, "");
_Static_assert(offsetof(struct D3D11_BUFFER_DESC, CPUAccessFlags) == 12, "");
_Static_assert(offsetof(struct D3D11_BUFFER_DESC, MiscFlags) == 16, "");
_Static_assert(offsetof(struct D3D11_BUFFER_DESC, StructureByteStride) == 20, "");

struct D3D11_SUBRESOURCE_DATA {
    const void *pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
};
typedef struct D3D11_SUBRESOURCE_DATA D3D11_SUBRESOURCE_DATA;
_Static_assert(sizeof(struct D3D11_SUBRESOURCE_DATA) == 16, "");
_Static_assert(offsetof(struct D3D11_SUBRESOURCE_DATA, pSysMem) == 0, "");
_Static_assert(offsetof(struct D3D11_SUBRESOURCE_DATA, SysMemPitch) == 8, "");
_Static_assert(offsetof(struct D3D11_SUBRESOURCE_DATA, SysMemSlicePitch) == 12, "");

struct D3D11_MAPPED_SUBRESOURCE {
    void *pData;
    UINT RowPitch;
    UINT DepthPitch;
};
typedef struct D3D11_MAPPED_SUBRESOURCE D3D11_MAPPED_SUBRESOURCE;
_Static_assert(sizeof(struct D3D11_MAPPED_SUBRESOURCE) == 16, "");
_Static_assert(offsetof(struct D3D11_MAPPED_SUBRESOURCE, pData) == 0, "");
_Static_assert(offsetof(struct D3D11_MAPPED_SUBRESOURCE, RowPitch) == 8, "");
_Static_assert(offsetof(struct D3D11_MAPPED_SUBRESOURCE, DepthPitch) == 12, "");

struct D3D11_BOX {
    UINT left;
    UINT top;
    UINT front;
    UINT right;
    UINT bottom;
    UINT back;
};
typedef struct D3D11_BOX D3D11_BOX;
_Static_assert(sizeof(struct D3D11_BOX) == 24, "");
_Static_assert(offsetof(struct D3D11_BOX, left) == 0, "");
_Static_assert(offsetof(struct D3D11_BOX, top) == 4, "");
_Static_assert(offsetof(struct D3D11_BOX, front) == 8, "");
_Static_assert(offsetof(struct D3D11_BOX, right) == 12, "");
_Static_assert(offsetof(struct D3D11_BOX, bottom) == 16, "");
_Static_assert(offsetof(struct D3D11_BOX, back) == 20, "");

struct D3D11_VIEWPORT {
    FLOAT TopLeftX;
    FLOAT TopLeftY;
    FLOAT Width;
    FLOAT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
};
typedef struct D3D11_VIEWPORT D3D11_VIEWPORT;
_Static_assert(sizeof(struct D3D11_VIEWPORT) == 24, "");
_Static_assert(offsetof(struct D3D11_VIEWPORT, TopLeftX) == 0, "");
_Static_assert(offsetof(struct D3D11_VIEWPORT, TopLeftY) == 4, "");
_Static_assert(offsetof(struct D3D11_VIEWPORT, Width) == 8, "");
_Static_assert(offsetof(struct D3D11_VIEWPORT, Height) == 12, "");
_Static_assert(offsetof(struct D3D11_VIEWPORT, MinDepth) == 16, "");
_Static_assert(offsetof(struct D3D11_VIEWPORT, MaxDepth) == 20, "");

struct D3D11_TEXTURE1D_DESC {
    UINT Width;
    UINT MipLevels;
    UINT ArraySize;
    UINT Format;                 /* DXGI_FORMAT */
    UINT Usage;                  /* D3D11_USAGE */
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};
typedef struct D3D11_TEXTURE1D_DESC D3D11_TEXTURE1D_DESC;
_Static_assert(sizeof(struct D3D11_TEXTURE1D_DESC) == 32, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, Width) == 0, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, MipLevels) == 4, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, ArraySize) == 8, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, Format) == 12, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, Usage) == 16, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, BindFlags) == 20, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, CPUAccessFlags) == 24, "");
_Static_assert(offsetof(struct D3D11_TEXTURE1D_DESC, MiscFlags) == 28, "");

struct D3D11_TEXTURE2D_DESC {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    UINT Format;                 /* DXGI_FORMAT */
    struct DXGI_SAMPLE_DESC SampleDesc;
    UINT Usage;                  /* D3D11_USAGE */
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};
typedef struct D3D11_TEXTURE2D_DESC D3D11_TEXTURE2D_DESC;
_Static_assert(sizeof(struct D3D11_TEXTURE2D_DESC) == 44, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, Width) == 0, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, Height) == 4, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, MipLevels) == 8, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, ArraySize) == 12, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, Format) == 16, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, SampleDesc) == 20, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, Usage) == 28, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, BindFlags) == 32, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, CPUAccessFlags) == 36, "");
_Static_assert(offsetof(struct D3D11_TEXTURE2D_DESC, MiscFlags) == 40, "");

struct D3D11_TEXTURE3D_DESC {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    UINT Format;                 /* DXGI_FORMAT */
    UINT Usage;                  /* D3D11_USAGE */
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};
typedef struct D3D11_TEXTURE3D_DESC D3D11_TEXTURE3D_DESC;
_Static_assert(sizeof(struct D3D11_TEXTURE3D_DESC) == 36, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, Width) == 0, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, Height) == 4, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, Depth) == 8, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, MipLevels) == 12, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, Format) == 16, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, Usage) == 20, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, BindFlags) == 24, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, CPUAccessFlags) == 28, "");
_Static_assert(offsetof(struct D3D11_TEXTURE3D_DESC, MiscFlags) == 32, "");

struct D3D11_QUERY_DESC {
    UINT Query;                  /* D3D11_QUERY */
    UINT MiscFlags;
};
typedef struct D3D11_QUERY_DESC D3D11_QUERY_DESC;
_Static_assert(sizeof(struct D3D11_QUERY_DESC) == 8, "");
_Static_assert(offsetof(struct D3D11_QUERY_DESC, Query) == 0, "");
_Static_assert(offsetof(struct D3D11_QUERY_DESC, MiscFlags) == 4, "");

struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT {
    UINT64 Frequency;
    BOOL Disjoint;
};
typedef struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT D3D11_QUERY_DATA_TIMESTAMP_DISJOINT;
_Static_assert(sizeof(struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT) == 16, "");
_Static_assert(offsetof(struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT, Frequency) == 0, "");
_Static_assert(offsetof(struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT, Disjoint) == 8, "");

struct D3D11_INPUT_ELEMENT_DESC {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    UINT Format;                 /* DXGI_FORMAT */
    UINT InputSlot;
    UINT AlignedByteOffset;
    UINT InputSlotClass;         /* D3D11_INPUT_CLASSIFICATION */
    UINT InstanceDataStepRate;
};
typedef struct D3D11_INPUT_ELEMENT_DESC D3D11_INPUT_ELEMENT_DESC;
_Static_assert(sizeof(struct D3D11_INPUT_ELEMENT_DESC) == 32, "");
_Static_assert(offsetof(struct D3D11_INPUT_ELEMENT_DESC, SemanticName) == 0, "");
_Static_assert(offsetof(struct D3D11_INPUT_ELEMENT_DESC, SemanticIndex) == 8, "");
_Static_assert(offsetof(struct D3D11_INPUT_ELEMENT_DESC, Format) == 12, "");
_Static_assert(offsetof(struct D3D11_INPUT_ELEMENT_DESC, InputSlot) == 16, "");
_Static_assert(offsetof(struct D3D11_INPUT_ELEMENT_DESC, AlignedByteOffset) == 20, "");
_Static_assert(offsetof(struct D3D11_INPUT_ELEMENT_DESC, InputSlotClass) == 24, "");
_Static_assert(offsetof(struct D3D11_INPUT_ELEMENT_DESC, InstanceDataStepRate) == 28, "");

struct D3D11_SAMPLER_DESC {
    UINT Filter;                 /* D3D11_FILTER */
    UINT AddressU;               /* D3D11_TEXTURE_ADDRESS_MODE */
    UINT AddressV;               /* D3D11_TEXTURE_ADDRESS_MODE */
    UINT AddressW;               /* D3D11_TEXTURE_ADDRESS_MODE */
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    UINT ComparisonFunc;         /* D3D11_COMPARISON_FUNC */
    FLOAT BorderColor[4];
    FLOAT MinLOD;
    FLOAT MaxLOD;
};
typedef struct D3D11_SAMPLER_DESC D3D11_SAMPLER_DESC;
_Static_assert(sizeof(struct D3D11_SAMPLER_DESC) == 52, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, Filter) == 0, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, AddressU) == 4, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, AddressV) == 8, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, AddressW) == 12, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, MipLODBias) == 16, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, MaxAnisotropy) == 20, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, ComparisonFunc) == 24, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, BorderColor) == 28, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, MinLOD) == 44, "");
_Static_assert(offsetof(struct D3D11_SAMPLER_DESC, MaxLOD) == 48, "");

struct D3D11_RASTERIZER_DESC {
    UINT FillMode;               /* D3D11_FILL_MODE */
    UINT CullMode;               /* D3D11_CULL_MODE */
    BOOL FrontCounterClockwise;
    INT DepthBias;
    FLOAT DepthBiasClamp;
    FLOAT SlopeScaledDepthBias;
    BOOL DepthClipEnable;
    BOOL ScissorEnable;
    BOOL MultisampleEnable;
    BOOL AntialiasedLineEnable;
};
typedef struct D3D11_RASTERIZER_DESC D3D11_RASTERIZER_DESC;
_Static_assert(sizeof(struct D3D11_RASTERIZER_DESC) == 40, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, FillMode) == 0, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, CullMode) == 4, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, FrontCounterClockwise) == 8, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, DepthBias) == 12, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, DepthBiasClamp) == 16, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, SlopeScaledDepthBias) == 20, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, DepthClipEnable) == 24, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, ScissorEnable) == 28, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, MultisampleEnable) == 32, "");
_Static_assert(offsetof(struct D3D11_RASTERIZER_DESC, AntialiasedLineEnable) == 36, "");

struct D3D11_DEPTH_STENCILOP_DESC {
    UINT StencilFailOp;          /* D3D11_STENCIL_OP */
    UINT StencilDepthFailOp;     /* D3D11_STENCIL_OP */
    UINT StencilPassOp;          /* D3D11_STENCIL_OP */
    UINT StencilFunc;            /* D3D11_COMPARISON_FUNC */
};
typedef struct D3D11_DEPTH_STENCILOP_DESC D3D11_DEPTH_STENCILOP_DESC;
_Static_assert(sizeof(struct D3D11_DEPTH_STENCILOP_DESC) == 16, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCILOP_DESC, StencilFailOp) == 0, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCILOP_DESC, StencilDepthFailOp) == 4, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCILOP_DESC, StencilPassOp) == 8, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCILOP_DESC, StencilFunc) == 12, "");

struct D3D11_DEPTH_STENCIL_DESC {
    BOOL DepthEnable;
    UINT DepthWriteMask;         /* D3D11_DEPTH_WRITE_MASK */
    UINT DepthFunc;              /* D3D11_COMPARISON_FUNC */
    BOOL StencilEnable;
    UINT8 StencilReadMask;
    UINT8 StencilWriteMask;
    struct D3D11_DEPTH_STENCILOP_DESC FrontFace;
    struct D3D11_DEPTH_STENCILOP_DESC BackFace;
};
typedef struct D3D11_DEPTH_STENCIL_DESC D3D11_DEPTH_STENCIL_DESC;
_Static_assert(sizeof(struct D3D11_DEPTH_STENCIL_DESC) == 52, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, DepthEnable) == 0, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, DepthWriteMask) == 4, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, DepthFunc) == 8, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, StencilEnable) == 12, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, StencilReadMask) == 16, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, StencilWriteMask) == 17, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, FrontFace) == 20, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_DESC, BackFace) == 36, "");

struct D3D11_RENDER_TARGET_BLEND_DESC {
    BOOL BlendEnable;
    UINT SrcBlend;               /* D3D11_BLEND */
    UINT DestBlend;              /* D3D11_BLEND */
    UINT BlendOp;                /* D3D11_BLEND_OP */
    UINT SrcBlendAlpha;          /* D3D11_BLEND */
    UINT DestBlendAlpha;         /* D3D11_BLEND */
    UINT BlendOpAlpha;           /* D3D11_BLEND_OP */
    UINT8 RenderTargetWriteMask;
};
typedef struct D3D11_RENDER_TARGET_BLEND_DESC D3D11_RENDER_TARGET_BLEND_DESC;
_Static_assert(sizeof(struct D3D11_RENDER_TARGET_BLEND_DESC) == 32, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, BlendEnable) == 0, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, SrcBlend) == 4, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, DestBlend) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, BlendOp) == 12, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, SrcBlendAlpha) == 16, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, DestBlendAlpha) == 20, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, BlendOpAlpha) == 24, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_BLEND_DESC, RenderTargetWriteMask) == 28, "");

struct D3D11_BLEND_DESC {
    BOOL AlphaToCoverageEnable;
    BOOL IndependentBlendEnable;
    struct D3D11_RENDER_TARGET_BLEND_DESC RenderTarget[8];
};
typedef struct D3D11_BLEND_DESC D3D11_BLEND_DESC;
_Static_assert(sizeof(struct D3D11_BLEND_DESC) == 264, "");
_Static_assert(offsetof(struct D3D11_BLEND_DESC, AlphaToCoverageEnable) == 0, "");
_Static_assert(offsetof(struct D3D11_BLEND_DESC, IndependentBlendEnable) == 4, "");
_Static_assert(offsetof(struct D3D11_BLEND_DESC, RenderTarget) == 8, "");

struct D3D11_SIGNATURE_PARAMETER_DESC {
    LPCSTR SemanticName;         /* const char * in MinGW */
    UINT SemanticIndex;
    UINT Register;
    UINT SystemValueType;        /* D3D_NAME */
    UINT ComponentType;          /* D3D_REGISTER_COMPONENT_TYPE */
    BYTE Mask;
    BYTE ReadWriteMask;
    UINT Stream;
    UINT MinPrecision;           /* D3D_MIN_PRECISION (D3D_COMPILER_VERSION 47) */
};
typedef struct D3D11_SIGNATURE_PARAMETER_DESC D3D11_SIGNATURE_PARAMETER_DESC;
_Static_assert(sizeof(struct D3D11_SIGNATURE_PARAMETER_DESC) == 40, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, SemanticName) == 0, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, SemanticIndex) == 8, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, Register) == 12, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, SystemValueType) == 16, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, ComponentType) == 20, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, Mask) == 24, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, ReadWriteMask) == 25, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, Stream) == 28, "");
_Static_assert(offsetof(struct D3D11_SIGNATURE_PARAMETER_DESC, MinPrecision) == 32, "");

/* D3D_FEATURE_LEVEL, the other levels (MinGW d3dcommon.h). */
#define D3D_FEATURE_LEVEL_9_1      0x9100u
#define D3D_FEATURE_LEVEL_9_2      0x9200u
#define D3D_FEATURE_LEVEL_9_3      0x9300u
#define D3D_FEATURE_LEVEL_10_0     0xa000u
#define D3D_FEATURE_LEVEL_10_1     0xa100u
#define D3D_FEATURE_LEVEL_11_1     0xb100u

/* D3D11_USAGE (MinGW d3d11.h). */
#define D3D11_USAGE_DEFAULT        0u
#define D3D11_USAGE_IMMUTABLE      1u
#define D3D11_USAGE_DYNAMIC        2u
#define D3D11_USAGE_STAGING        3u

/* D3D11_BIND_FLAG (MinGW d3d11.h). */
#define D3D11_BIND_VERTEX_BUFFER       0x1u
#define D3D11_BIND_INDEX_BUFFER        0x2u
#define D3D11_BIND_CONSTANT_BUFFER     0x4u
#define D3D11_BIND_SHADER_RESOURCE     0x8u
#define D3D11_BIND_STREAM_OUTPUT       0x10u
#define D3D11_BIND_RENDER_TARGET       0x20u
#define D3D11_BIND_DEPTH_STENCIL       0x40u
#define D3D11_BIND_UNORDERED_ACCESS    0x80u
#define D3D11_BIND_DECODER             0x200u
#define D3D11_BIND_VIDEO_ENCODER       0x400u

/* D3D11_CPU_ACCESS_FLAG (MinGW d3d11.h). */
#define D3D11_CPU_ACCESS_WRITE     0x10000u
#define D3D11_CPU_ACCESS_READ      0x20000u

/* D3D11_RESOURCE_MISC_FLAG (MinGW d3d11.h). */
#define D3D11_RESOURCE_MISC_GENERATE_MIPS                    0x1u
#define D3D11_RESOURCE_MISC_SHARED                           0x2u
#define D3D11_RESOURCE_MISC_TEXTURECUBE                      0x4u
#define D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS                0x10u
#define D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS           0x20u
#define D3D11_RESOURCE_MISC_BUFFER_STRUCTURED                0x40u
#define D3D11_RESOURCE_MISC_RESOURCE_CLAMP                   0x80u
#define D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX                0x100u
#define D3D11_RESOURCE_MISC_GDI_COMPATIBLE                   0x200u
#define D3D11_RESOURCE_MISC_SHARED_NTHANDLE                  0x800u
#define D3D11_RESOURCE_MISC_RESTRICTED_CONTENT               0x1000u
#define D3D11_RESOURCE_MISC_RESTRICT_SHARED_RESOURCE         0x2000u
#define D3D11_RESOURCE_MISC_RESTRICT_SHARED_RESOURCE_DRIVER  0x4000u
#define D3D11_RESOURCE_MISC_GUARDED                          0x8000u
#define D3D11_RESOURCE_MISC_TILE_POOL                        0x20000u
#define D3D11_RESOURCE_MISC_TILED                            0x40000u
#define D3D11_RESOURCE_MISC_HW_PROTECTED                     0x80000u

/* D3D11_MAP, D3D11_MAP_FLAG (MinGW d3d11.h). */
#define D3D11_MAP_READ                 1u
#define D3D11_MAP_WRITE                2u
#define D3D11_MAP_READ_WRITE           3u
#define D3D11_MAP_WRITE_DISCARD        4u
#define D3D11_MAP_WRITE_NO_OVERWRITE   5u
#define D3D11_MAP_FLAG_DO_NOT_WAIT     0x100000u

/* D3D_PRIMITIVE_TOPOLOGY, the D3D11_PRIMITIVE_TOPOLOGY_ names (MinGW d3dcommon.h). */
#define D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED         0u
#define D3D11_PRIMITIVE_TOPOLOGY_POINTLIST         1u
#define D3D11_PRIMITIVE_TOPOLOGY_LINELIST          2u
#define D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP         3u
#define D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST      4u
#define D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP     5u
#define D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ      10u
#define D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ     11u
#define D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ  12u
#define D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ 13u

/* D3D11_QUERY (MinGW d3d11.h). */
#define D3D11_QUERY_EVENT                              0u
#define D3D11_QUERY_OCCLUSION                          1u
#define D3D11_QUERY_TIMESTAMP                          2u
#define D3D11_QUERY_TIMESTAMP_DISJOINT                 3u
#define D3D11_QUERY_PIPELINE_STATISTICS                4u
#define D3D11_QUERY_OCCLUSION_PREDICATE                5u
#define D3D11_QUERY_SO_STATISTICS                      6u
#define D3D11_QUERY_SO_OVERFLOW_PREDICATE              7u
#define D3D11_QUERY_SO_STATISTICS_STREAM0              8u
#define D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0      9u
#define D3D11_QUERY_SO_STATISTICS_STREAM1              10u
#define D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1      11u
#define D3D11_QUERY_SO_STATISTICS_STREAM2              12u
#define D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2      13u
#define D3D11_QUERY_SO_STATISTICS_STREAM3              14u
#define D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3      15u

/* D3D11_ASYNC_GETDATA_FLAG (MinGW d3d11.h). */
#define D3D11_ASYNC_GETDATA_DONOTFLUSH     0x1u

/* D3D11_FORMAT_SUPPORT (MinGW d3d11.h). */
#define D3D11_FORMAT_SUPPORT_BUFFER                          0x1u
#define D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER                0x2u
#define D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER                 0x4u
#define D3D11_FORMAT_SUPPORT_SO_BUFFER                       0x8u
#define D3D11_FORMAT_SUPPORT_TEXTURE1D                       0x10u
#define D3D11_FORMAT_SUPPORT_TEXTURE2D                       0x20u
#define D3D11_FORMAT_SUPPORT_TEXTURE3D                       0x40u
#define D3D11_FORMAT_SUPPORT_TEXTURECUBE                     0x80u
#define D3D11_FORMAT_SUPPORT_SHADER_LOAD                     0x100u
#define D3D11_FORMAT_SUPPORT_SHADER_SAMPLE                   0x200u
#define D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON        0x400u
#define D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_MONO_TEXT         0x800u
#define D3D11_FORMAT_SUPPORT_MIP                             0x1000u
#define D3D11_FORMAT_SUPPORT_MIP_AUTOGEN                     0x2000u
#define D3D11_FORMAT_SUPPORT_RENDER_TARGET                   0x4000u
#define D3D11_FORMAT_SUPPORT_BLENDABLE                       0x8000u
#define D3D11_FORMAT_SUPPORT_DEPTH_STENCIL                   0x10000u
#define D3D11_FORMAT_SUPPORT_CPU_LOCKABLE                    0x20000u
#define D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE             0x40000u
#define D3D11_FORMAT_SUPPORT_DISPLAY                         0x80000u
#define D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT          0x100000u
#define D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET        0x200000u
#define D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD                0x400000u
#define D3D11_FORMAT_SUPPORT_SHADER_GATHER                   0x800000u
#define D3D11_FORMAT_SUPPORT_BACK_BUFFER_CAST                0x1000000u
#define D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW     0x2000000u
#define D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON        0x4000000u
#define D3D11_FORMAT_SUPPORT_DECODER_OUTPUT                  0x8000000u
#define D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT          0x10000000u
#define D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT           0x20000000u
#define D3D11_FORMAT_SUPPORT_VIDEO_ENCODER                   0x40000000u

/* D3D11_RESOURCE_DIMENSION (MinGW d3d11.h). */
#define D3D11_RESOURCE_DIMENSION_UNKNOWN   0u
#define D3D11_RESOURCE_DIMENSION_BUFFER    1u
#define D3D11_RESOURCE_DIMENSION_TEXTURE1D 2u
#define D3D11_RESOURCE_DIMENSION_TEXTURE2D 3u
#define D3D11_RESOURCE_DIMENSION_TEXTURE3D 4u

/* D3D11_FILL_MODE (MinGW d3d11.h). */
#define D3D11_FILL_WIREFRAME       2u
#define D3D11_FILL_SOLID           3u

/* D3D11_CULL_MODE (MinGW d3d11.h). */
#define D3D11_CULL_NONE            1u
#define D3D11_CULL_FRONT           2u
#define D3D11_CULL_BACK            3u

/* D3D11_BLEND (MinGW d3d11.h). */
#define D3D11_BLEND_ZERO               1u
#define D3D11_BLEND_ONE                2u
#define D3D11_BLEND_SRC_COLOR          3u
#define D3D11_BLEND_INV_SRC_COLOR      4u
#define D3D11_BLEND_SRC_ALPHA          5u
#define D3D11_BLEND_INV_SRC_ALPHA      6u
#define D3D11_BLEND_DEST_ALPHA         7u
#define D3D11_BLEND_INV_DEST_ALPHA     8u
#define D3D11_BLEND_DEST_COLOR         9u
#define D3D11_BLEND_INV_DEST_COLOR     10u
#define D3D11_BLEND_SRC_ALPHA_SAT      11u
#define D3D11_BLEND_BLEND_FACTOR       14u
#define D3D11_BLEND_INV_BLEND_FACTOR   15u
#define D3D11_BLEND_SRC1_COLOR         16u
#define D3D11_BLEND_INV_SRC1_COLOR     17u
#define D3D11_BLEND_SRC1_ALPHA         18u
#define D3D11_BLEND_INV_SRC1_ALPHA     19u

/* D3D11_BLEND_OP (MinGW d3d11.h). */
#define D3D11_BLEND_OP_ADD             1u
#define D3D11_BLEND_OP_SUBTRACT        2u
#define D3D11_BLEND_OP_REV_SUBTRACT    3u
#define D3D11_BLEND_OP_MIN             4u
#define D3D11_BLEND_OP_MAX             5u

/* D3D11_COLOR_WRITE_ENABLE (MinGW d3d11.h). */
#define D3D11_COLOR_WRITE_ENABLE_RED   1u
#define D3D11_COLOR_WRITE_ENABLE_GREEN 2u
#define D3D11_COLOR_WRITE_ENABLE_BLUE  4u
#define D3D11_COLOR_WRITE_ENABLE_ALPHA 8u
#define D3D11_COLOR_WRITE_ENABLE_ALL   15u

/* D3D11_COMPARISON_FUNC (MinGW d3d11.h). */
#define D3D11_COMPARISON_NEVER         1u
#define D3D11_COMPARISON_LESS          2u
#define D3D11_COMPARISON_EQUAL         3u
#define D3D11_COMPARISON_LESS_EQUAL    4u
#define D3D11_COMPARISON_GREATER       5u
#define D3D11_COMPARISON_NOT_EQUAL     6u
#define D3D11_COMPARISON_GREATER_EQUAL 7u
#define D3D11_COMPARISON_ALWAYS        8u

/* D3D11_STENCIL_OP (MinGW d3d11.h). */
#define D3D11_STENCIL_OP_KEEP          1u
#define D3D11_STENCIL_OP_ZERO          2u
#define D3D11_STENCIL_OP_REPLACE       3u
#define D3D11_STENCIL_OP_INCR_SAT      4u
#define D3D11_STENCIL_OP_DECR_SAT      5u
#define D3D11_STENCIL_OP_INVERT        6u
#define D3D11_STENCIL_OP_INCR          7u
#define D3D11_STENCIL_OP_DECR          8u

/* D3D11_DEPTH_WRITE_MASK (MinGW d3d11.h). */
#define D3D11_DEPTH_WRITE_MASK_ZERO    0u
#define D3D11_DEPTH_WRITE_MASK_ALL     1u

/* D3D11_TEXTURE_ADDRESS_MODE (MinGW d3d11.h). */
#define D3D11_TEXTURE_ADDRESS_WRAP        1u
#define D3D11_TEXTURE_ADDRESS_MIRROR      2u
#define D3D11_TEXTURE_ADDRESS_CLAMP       3u
#define D3D11_TEXTURE_ADDRESS_BORDER      4u
#define D3D11_TEXTURE_ADDRESS_MIRROR_ONCE 5u

/* D3D11_FILTER (MinGW d3d11.h). */
#define D3D11_FILTER_MIN_MAG_MIP_POINT                              0x0u
#define D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR                       0x1u
#define D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT                 0x4u
#define D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR                       0x5u
#define D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT                       0x10u
#define D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR                0x11u
#define D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT                       0x14u
#define D3D11_FILTER_MIN_MAG_MIP_LINEAR                             0x15u
#define D3D11_FILTER_ANISOTROPIC                                    0x55u
#define D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT                   0x80u
#define D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR            0x81u
#define D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT      0x84u
#define D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR            0x85u
#define D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT            0x90u
#define D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR     0x91u
#define D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT            0x94u
#define D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR                  0x95u
#define D3D11_FILTER_COMPARISON_ANISOTROPIC                         0xd5u
#define D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT                      0x100u
#define D3D11_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR               0x101u
#define D3D11_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT         0x104u
#define D3D11_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR               0x105u
#define D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT               0x110u
#define D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR        0x111u
#define D3D11_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT               0x114u
#define D3D11_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR                     0x115u
#define D3D11_FILTER_MINIMUM_ANISOTROPIC                            0x155u
#define D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT                      0x180u
#define D3D11_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR               0x181u
#define D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT         0x184u
#define D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR               0x185u
#define D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT               0x190u
#define D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR        0x191u
#define D3D11_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT               0x194u
#define D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR                     0x195u
#define D3D11_FILTER_MAXIMUM_ANISOTROPIC                            0x1d5u

/* D3D11_INPUT_CLASSIFICATION (MinGW d3d11.h). */
#define D3D11_INPUT_PER_VERTEX_DATA    0u
#define D3D11_INPUT_PER_INSTANCE_DATA  1u

/* D3D11_CLEAR_FLAG (MinGW d3d11.h). */
#define D3D11_CLEAR_DEPTH              0x1u
#define D3D11_CLEAR_STENCIL            0x2u

/* Limits and defaults (MinGW d3d11.h, its #define lines). */
#define D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT          14u
#define D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT               128u
#define D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT                      16u
#define D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT                     8u
#define D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE   16u
#define D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT                  32u
#define D3D11_PS_CS_UAV_REGISTER_COUNT                             8u
#define D3D11_REQ_TEXTURE1D_U_DIMENSION                            16384u
#define D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION                       16384u
#define D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION                     2048u
#define D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION                   2048u
#define D3D11_REQ_MIP_LEVELS                                       15u
#define D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL                0xffffffffu
#define D3D11_KEEP_UNORDERED_ACCESS_VIEWS                          0xffffffffu
#define D3D11_DEFAULT_SAMPLE_MASK                                  0xffffffffu
#define D3D11_DEFAULT_STENCIL_REFERENCE                            0u
#define D3D11_DEFAULT_STENCIL_READ_MASK                            0xffu
#define D3D11_DEFAULT_STENCIL_WRITE_MASK                           0xffu
#define D3D11_APPEND_ALIGNED_ELEMENT                               0xffffffffu

#include "rt/win/d3d11view.h"   /* the four view descriptions and their dimensions */

#endif /* RT_WIN_D3D11_H */
