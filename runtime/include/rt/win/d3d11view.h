/* Direct3D 11: the descriptions of the four kinds of view (d3d11.h), with the structures
 * their unions are made of and the enumerators that say which member is meant.  Planner-owned;
 * rt/win/d3d11.h includes it.
 *
 * Rules and source as in rt/win/dxgi.h: `struct NAME { ... }; typedef struct NAME NAME;`, a
 * _Static_assert on every size and on the offset of every field (for a union: of every
 * member), enumerations as UINT with #defines for the enumerators; every number is the
 * MinGW-w64 d3d11.h's (third_party/dxmt/include/native/directx), 64-bit Windows layout, and
 * `make d3d-layout-check` compares each with that header.
 */
#ifndef RT_WIN_D3D11VIEW_H
#define RT_WIN_D3D11VIEW_H

#include <stddef.h>

#include "rt/wintypes.h"

/* d3d11.h: shader-resource views */
struct D3D11_BUFFER_SRV {
    union { UINT FirstElement; UINT ElementOffset; };
    union { UINT NumElements; UINT ElementWidth; };
};
typedef struct D3D11_BUFFER_SRV D3D11_BUFFER_SRV;
_Static_assert(sizeof(struct D3D11_BUFFER_SRV) == 8, "");
_Static_assert(offsetof(struct D3D11_BUFFER_SRV, FirstElement) == 0, "");
_Static_assert(offsetof(struct D3D11_BUFFER_SRV, ElementOffset) == 0, "");
_Static_assert(offsetof(struct D3D11_BUFFER_SRV, NumElements) == 4, "");
_Static_assert(offsetof(struct D3D11_BUFFER_SRV, ElementWidth) == 4, "");

struct D3D11_BUFFEREX_SRV { UINT FirstElement; UINT NumElements; UINT Flags; };
typedef struct D3D11_BUFFEREX_SRV D3D11_BUFFEREX_SRV;
_Static_assert(sizeof(struct D3D11_BUFFEREX_SRV) == 12, "");
_Static_assert(offsetof(struct D3D11_BUFFEREX_SRV, FirstElement) == 0, "");
_Static_assert(offsetof(struct D3D11_BUFFEREX_SRV, NumElements) == 4, "");
_Static_assert(offsetof(struct D3D11_BUFFEREX_SRV, Flags) == 8, "");

struct D3D11_TEX1D_SRV { UINT MostDetailedMip; UINT MipLevels; };
typedef struct D3D11_TEX1D_SRV D3D11_TEX1D_SRV;
_Static_assert(sizeof(struct D3D11_TEX1D_SRV) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX1D_SRV, MostDetailedMip) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX1D_SRV, MipLevels) == 4, "");

struct D3D11_TEX1D_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX1D_ARRAY_SRV D3D11_TEX1D_ARRAY_SRV;
_Static_assert(sizeof(struct D3D11_TEX1D_ARRAY_SRV) == 16, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_SRV, MostDetailedMip) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_SRV, MipLevels) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_SRV, FirstArraySlice) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_SRV, ArraySize) == 12, "");

struct D3D11_TEX2D_SRV { UINT MostDetailedMip; UINT MipLevels; };
typedef struct D3D11_TEX2D_SRV D3D11_TEX2D_SRV;
_Static_assert(sizeof(struct D3D11_TEX2D_SRV) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX2D_SRV, MostDetailedMip) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2D_SRV, MipLevels) == 4, "");

struct D3D11_TEX2D_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX2D_ARRAY_SRV D3D11_TEX2D_ARRAY_SRV;
_Static_assert(sizeof(struct D3D11_TEX2D_ARRAY_SRV) == 16, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_SRV, MostDetailedMip) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_SRV, MipLevels) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_SRV, FirstArraySlice) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_SRV, ArraySize) == 12, "");

struct D3D11_TEX3D_SRV { UINT MostDetailedMip; UINT MipLevels; };
typedef struct D3D11_TEX3D_SRV D3D11_TEX3D_SRV;
_Static_assert(sizeof(struct D3D11_TEX3D_SRV) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX3D_SRV, MostDetailedMip) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX3D_SRV, MipLevels) == 4, "");

struct D3D11_TEXCUBE_SRV { UINT MostDetailedMip; UINT MipLevels; };
typedef struct D3D11_TEXCUBE_SRV D3D11_TEXCUBE_SRV;
_Static_assert(sizeof(struct D3D11_TEXCUBE_SRV) == 8, "");
_Static_assert(offsetof(struct D3D11_TEXCUBE_SRV, MostDetailedMip) == 0, "");
_Static_assert(offsetof(struct D3D11_TEXCUBE_SRV, MipLevels) == 4, "");

struct D3D11_TEXCUBE_ARRAY_SRV { UINT MostDetailedMip; UINT MipLevels; UINT First2DArrayFace; UINT NumCubes; };
typedef struct D3D11_TEXCUBE_ARRAY_SRV D3D11_TEXCUBE_ARRAY_SRV;
_Static_assert(sizeof(struct D3D11_TEXCUBE_ARRAY_SRV) == 16, "");
_Static_assert(offsetof(struct D3D11_TEXCUBE_ARRAY_SRV, MostDetailedMip) == 0, "");
_Static_assert(offsetof(struct D3D11_TEXCUBE_ARRAY_SRV, MipLevels) == 4, "");
_Static_assert(offsetof(struct D3D11_TEXCUBE_ARRAY_SRV, First2DArrayFace) == 8, "");
_Static_assert(offsetof(struct D3D11_TEXCUBE_ARRAY_SRV, NumCubes) == 12, "");

struct D3D11_TEX2DMS_SRV { UINT UnusedField_NothingToDefine; };
typedef struct D3D11_TEX2DMS_SRV D3D11_TEX2DMS_SRV;
_Static_assert(sizeof(struct D3D11_TEX2DMS_SRV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_SRV, UnusedField_NothingToDefine) == 0, "");

struct D3D11_TEX2DMS_ARRAY_SRV { UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX2DMS_ARRAY_SRV D3D11_TEX2DMS_ARRAY_SRV;
_Static_assert(sizeof(struct D3D11_TEX2DMS_ARRAY_SRV) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_ARRAY_SRV, FirstArraySlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_ARRAY_SRV, ArraySize) == 4, "");

struct D3D11_SHADER_RESOURCE_VIEW_DESC {
    UINT Format;
    UINT ViewDimension;
    union {
        struct D3D11_BUFFER_SRV Buffer;
        struct D3D11_TEX1D_SRV Texture1D;
        struct D3D11_TEX1D_ARRAY_SRV Texture1DArray;
        struct D3D11_TEX2D_SRV Texture2D;
        struct D3D11_TEX2D_ARRAY_SRV Texture2DArray;
        struct D3D11_TEX2DMS_SRV Texture2DMS;
        struct D3D11_TEX2DMS_ARRAY_SRV Texture2DMSArray;
        struct D3D11_TEX3D_SRV Texture3D;
        struct D3D11_TEXCUBE_SRV TextureCube;
        struct D3D11_TEXCUBE_ARRAY_SRV TextureCubeArray;
        struct D3D11_BUFFEREX_SRV BufferEx;
    };
};
typedef struct D3D11_SHADER_RESOURCE_VIEW_DESC D3D11_SHADER_RESOURCE_VIEW_DESC;
_Static_assert(sizeof(struct D3D11_SHADER_RESOURCE_VIEW_DESC) == 24, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Format) == 0, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, ViewDimension) == 4, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Buffer) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Texture1D) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Texture1DArray) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Texture2D) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Texture2DArray) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Texture2DMS) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Texture2DMSArray) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, Texture3D) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, TextureCube) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, TextureCubeArray) == 8, "");
_Static_assert(offsetof(struct D3D11_SHADER_RESOURCE_VIEW_DESC, BufferEx) == 8, "");

/* d3d11.h: unordered-access views */
struct D3D11_BUFFER_UAV { UINT FirstElement; UINT NumElements; UINT Flags; };
typedef struct D3D11_BUFFER_UAV D3D11_BUFFER_UAV;
_Static_assert(sizeof(struct D3D11_BUFFER_UAV) == 12, "");
_Static_assert(offsetof(struct D3D11_BUFFER_UAV, FirstElement) == 0, "");
_Static_assert(offsetof(struct D3D11_BUFFER_UAV, NumElements) == 4, "");
_Static_assert(offsetof(struct D3D11_BUFFER_UAV, Flags) == 8, "");

struct D3D11_TEX1D_UAV { UINT MipSlice; };
typedef struct D3D11_TEX1D_UAV D3D11_TEX1D_UAV;
_Static_assert(sizeof(struct D3D11_TEX1D_UAV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX1D_UAV, MipSlice) == 0, "");

struct D3D11_TEX1D_ARRAY_UAV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX1D_ARRAY_UAV D3D11_TEX1D_ARRAY_UAV;
_Static_assert(sizeof(struct D3D11_TEX1D_ARRAY_UAV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_UAV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_UAV, FirstArraySlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_UAV, ArraySize) == 8, "");

struct D3D11_TEX2D_UAV { UINT MipSlice; };
typedef struct D3D11_TEX2D_UAV D3D11_TEX2D_UAV;
_Static_assert(sizeof(struct D3D11_TEX2D_UAV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2D_UAV, MipSlice) == 0, "");

struct D3D11_TEX2D_ARRAY_UAV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX2D_ARRAY_UAV D3D11_TEX2D_ARRAY_UAV;
_Static_assert(sizeof(struct D3D11_TEX2D_ARRAY_UAV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_UAV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_UAV, FirstArraySlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_UAV, ArraySize) == 8, "");

struct D3D11_TEX3D_UAV { UINT MipSlice; UINT FirstWSlice; UINT WSize; };
typedef struct D3D11_TEX3D_UAV D3D11_TEX3D_UAV;
_Static_assert(sizeof(struct D3D11_TEX3D_UAV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX3D_UAV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX3D_UAV, FirstWSlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX3D_UAV, WSize) == 8, "");

struct D3D11_UNORDERED_ACCESS_VIEW_DESC {
    UINT Format;
    UINT ViewDimension;
    union {
        struct D3D11_BUFFER_UAV Buffer;
        struct D3D11_TEX1D_UAV Texture1D;
        struct D3D11_TEX1D_ARRAY_UAV Texture1DArray;
        struct D3D11_TEX2D_UAV Texture2D;
        struct D3D11_TEX2D_ARRAY_UAV Texture2DArray;
        struct D3D11_TEX3D_UAV Texture3D;
    };
};
typedef struct D3D11_UNORDERED_ACCESS_VIEW_DESC D3D11_UNORDERED_ACCESS_VIEW_DESC;
_Static_assert(sizeof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC) == 20, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, Format) == 0, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, ViewDimension) == 4, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, Buffer) == 8, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, Texture1D) == 8, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, Texture1DArray) == 8, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, Texture2D) == 8, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, Texture2DArray) == 8, "");
_Static_assert(offsetof(struct D3D11_UNORDERED_ACCESS_VIEW_DESC, Texture3D) == 8, "");

/* d3d11.h: render-target views */
struct D3D11_BUFFER_RTV {
    union { UINT FirstElement; UINT ElementOffset; };
    union { UINT NumElements; UINT ElementWidth; };
};
typedef struct D3D11_BUFFER_RTV D3D11_BUFFER_RTV;
_Static_assert(sizeof(struct D3D11_BUFFER_RTV) == 8, "");
_Static_assert(offsetof(struct D3D11_BUFFER_RTV, FirstElement) == 0, "");
_Static_assert(offsetof(struct D3D11_BUFFER_RTV, ElementOffset) == 0, "");
_Static_assert(offsetof(struct D3D11_BUFFER_RTV, NumElements) == 4, "");
_Static_assert(offsetof(struct D3D11_BUFFER_RTV, ElementWidth) == 4, "");

struct D3D11_TEX1D_RTV { UINT MipSlice; };
typedef struct D3D11_TEX1D_RTV D3D11_TEX1D_RTV;
_Static_assert(sizeof(struct D3D11_TEX1D_RTV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX1D_RTV, MipSlice) == 0, "");

struct D3D11_TEX1D_ARRAY_RTV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX1D_ARRAY_RTV D3D11_TEX1D_ARRAY_RTV;
_Static_assert(sizeof(struct D3D11_TEX1D_ARRAY_RTV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_RTV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_RTV, FirstArraySlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_RTV, ArraySize) == 8, "");

struct D3D11_TEX2D_RTV { UINT MipSlice; };
typedef struct D3D11_TEX2D_RTV D3D11_TEX2D_RTV;
_Static_assert(sizeof(struct D3D11_TEX2D_RTV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2D_RTV, MipSlice) == 0, "");

struct D3D11_TEX2DMS_RTV { UINT UnusedField_NothingToDefine; };
typedef struct D3D11_TEX2DMS_RTV D3D11_TEX2DMS_RTV;
_Static_assert(sizeof(struct D3D11_TEX2DMS_RTV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_RTV, UnusedField_NothingToDefine) == 0, "");

struct D3D11_TEX2D_ARRAY_RTV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX2D_ARRAY_RTV D3D11_TEX2D_ARRAY_RTV;
_Static_assert(sizeof(struct D3D11_TEX2D_ARRAY_RTV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_RTV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_RTV, FirstArraySlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_RTV, ArraySize) == 8, "");

struct D3D11_TEX2DMS_ARRAY_RTV { UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX2DMS_ARRAY_RTV D3D11_TEX2DMS_ARRAY_RTV;
_Static_assert(sizeof(struct D3D11_TEX2DMS_ARRAY_RTV) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_ARRAY_RTV, FirstArraySlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_ARRAY_RTV, ArraySize) == 4, "");

struct D3D11_TEX3D_RTV { UINT MipSlice; UINT FirstWSlice; UINT WSize; };
typedef struct D3D11_TEX3D_RTV D3D11_TEX3D_RTV;
_Static_assert(sizeof(struct D3D11_TEX3D_RTV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX3D_RTV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX3D_RTV, FirstWSlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX3D_RTV, WSize) == 8, "");

struct D3D11_RENDER_TARGET_VIEW_DESC {
    UINT Format;
    UINT ViewDimension;
    union {
        struct D3D11_BUFFER_RTV Buffer;
        struct D3D11_TEX1D_RTV Texture1D;
        struct D3D11_TEX1D_ARRAY_RTV Texture1DArray;
        struct D3D11_TEX2D_RTV Texture2D;
        struct D3D11_TEX2D_ARRAY_RTV Texture2DArray;
        struct D3D11_TEX2DMS_RTV Texture2DMS;
        struct D3D11_TEX2DMS_ARRAY_RTV Texture2DMSArray;
        struct D3D11_TEX3D_RTV Texture3D;
    };
};
typedef struct D3D11_RENDER_TARGET_VIEW_DESC D3D11_RENDER_TARGET_VIEW_DESC;
_Static_assert(sizeof(struct D3D11_RENDER_TARGET_VIEW_DESC) == 20, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Format) == 0, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, ViewDimension) == 4, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Buffer) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Texture1D) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Texture1DArray) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Texture2D) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Texture2DArray) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Texture2DMS) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Texture2DMSArray) == 8, "");
_Static_assert(offsetof(struct D3D11_RENDER_TARGET_VIEW_DESC, Texture3D) == 8, "");

/* d3d11.h: depth-stencil views */
struct D3D11_TEX1D_DSV { UINT MipSlice; };
typedef struct D3D11_TEX1D_DSV D3D11_TEX1D_DSV;
_Static_assert(sizeof(struct D3D11_TEX1D_DSV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX1D_DSV, MipSlice) == 0, "");

struct D3D11_TEX1D_ARRAY_DSV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX1D_ARRAY_DSV D3D11_TEX1D_ARRAY_DSV;
_Static_assert(sizeof(struct D3D11_TEX1D_ARRAY_DSV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_DSV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_DSV, FirstArraySlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX1D_ARRAY_DSV, ArraySize) == 8, "");

struct D3D11_TEX2D_DSV { UINT MipSlice; };
typedef struct D3D11_TEX2D_DSV D3D11_TEX2D_DSV;
_Static_assert(sizeof(struct D3D11_TEX2D_DSV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2D_DSV, MipSlice) == 0, "");

struct D3D11_TEX2D_ARRAY_DSV { UINT MipSlice; UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX2D_ARRAY_DSV D3D11_TEX2D_ARRAY_DSV;
_Static_assert(sizeof(struct D3D11_TEX2D_ARRAY_DSV) == 12, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_DSV, MipSlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_DSV, FirstArraySlice) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2D_ARRAY_DSV, ArraySize) == 8, "");

struct D3D11_TEX2DMS_DSV { UINT UnusedField_NothingToDefine; };
typedef struct D3D11_TEX2DMS_DSV D3D11_TEX2DMS_DSV;
_Static_assert(sizeof(struct D3D11_TEX2DMS_DSV) == 4, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_DSV, UnusedField_NothingToDefine) == 0, "");

struct D3D11_TEX2DMS_ARRAY_DSV { UINT FirstArraySlice; UINT ArraySize; };
typedef struct D3D11_TEX2DMS_ARRAY_DSV D3D11_TEX2DMS_ARRAY_DSV;
_Static_assert(sizeof(struct D3D11_TEX2DMS_ARRAY_DSV) == 8, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_ARRAY_DSV, FirstArraySlice) == 0, "");
_Static_assert(offsetof(struct D3D11_TEX2DMS_ARRAY_DSV, ArraySize) == 4, "");

struct D3D11_DEPTH_STENCIL_VIEW_DESC {
    UINT Format;
    UINT ViewDimension;
    UINT Flags;
    union {
        struct D3D11_TEX1D_DSV Texture1D;
        struct D3D11_TEX1D_ARRAY_DSV Texture1DArray;
        struct D3D11_TEX2D_DSV Texture2D;
        struct D3D11_TEX2D_ARRAY_DSV Texture2DArray;
        struct D3D11_TEX2DMS_DSV Texture2DMS;
        struct D3D11_TEX2DMS_ARRAY_DSV Texture2DMSArray;
    };
};
typedef struct D3D11_DEPTH_STENCIL_VIEW_DESC D3D11_DEPTH_STENCIL_VIEW_DESC;
_Static_assert(sizeof(struct D3D11_DEPTH_STENCIL_VIEW_DESC) == 24, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Format) == 0, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, ViewDimension) == 4, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Flags) == 8, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Texture1D) == 12, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Texture1DArray) == 12, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Texture2D) == 12, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Texture2DArray) == 12, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Texture2DMS) == 12, "");
_Static_assert(offsetof(struct D3D11_DEPTH_STENCIL_VIEW_DESC, Texture2DMSArray) == 12, "");

/* D3D11_SRV_DIMENSION (its values are in d3dcommon.h) */
#define D3D11_SRV_DIMENSION_UNKNOWN 0u
#define D3D11_SRV_DIMENSION_BUFFER 1u
#define D3D11_SRV_DIMENSION_TEXTURE1D 2u
#define D3D11_SRV_DIMENSION_TEXTURE1DARRAY 3u
#define D3D11_SRV_DIMENSION_TEXTURE2D 4u
#define D3D11_SRV_DIMENSION_TEXTURE2DARRAY 5u
#define D3D11_SRV_DIMENSION_TEXTURE2DMS 6u
#define D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY 7u
#define D3D11_SRV_DIMENSION_TEXTURE3D 8u
#define D3D11_SRV_DIMENSION_TEXTURECUBE 9u
#define D3D11_SRV_DIMENSION_TEXTURECUBEARRAY 10u
#define D3D11_SRV_DIMENSION_BUFFEREX 11u

/* D3D11_UAV_DIMENSION */
#define D3D11_UAV_DIMENSION_UNKNOWN 0u
#define D3D11_UAV_DIMENSION_BUFFER 1u
#define D3D11_UAV_DIMENSION_TEXTURE1D 2u
#define D3D11_UAV_DIMENSION_TEXTURE1DARRAY 3u
#define D3D11_UAV_DIMENSION_TEXTURE2D 4u
#define D3D11_UAV_DIMENSION_TEXTURE2DARRAY 5u
#define D3D11_UAV_DIMENSION_TEXTURE3D 8u

/* D3D11_RTV_DIMENSION */
#define D3D11_RTV_DIMENSION_UNKNOWN 0u
#define D3D11_RTV_DIMENSION_BUFFER 1u
#define D3D11_RTV_DIMENSION_TEXTURE1D 2u
#define D3D11_RTV_DIMENSION_TEXTURE1DARRAY 3u
#define D3D11_RTV_DIMENSION_TEXTURE2D 4u
#define D3D11_RTV_DIMENSION_TEXTURE2DARRAY 5u
#define D3D11_RTV_DIMENSION_TEXTURE2DMS 6u
#define D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY 7u
#define D3D11_RTV_DIMENSION_TEXTURE3D 8u

/* D3D11_DSV_DIMENSION */
#define D3D11_DSV_DIMENSION_UNKNOWN 0u
#define D3D11_DSV_DIMENSION_TEXTURE1D 1u
#define D3D11_DSV_DIMENSION_TEXTURE1DARRAY 2u
#define D3D11_DSV_DIMENSION_TEXTURE2D 3u
#define D3D11_DSV_DIMENSION_TEXTURE2DARRAY 4u
#define D3D11_DSV_DIMENSION_TEXTURE2DMS 5u
#define D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY 6u

/* D3D11_UAV_FLAG (the Flags of D3D11_BUFFER_UAV) */
#define D3D11_BUFFER_UAV_FLAG_RAW 0x1u
#define D3D11_BUFFER_UAV_FLAG_APPEND 0x2u
#define D3D11_BUFFER_UAV_FLAG_COUNTER 0x4u

/* D3D11_BUFFEREX_SRV_FLAG */
#define D3D11_BUFFEREX_SRV_FLAG_RAW 0x1u

/* D3D11_DSV_FLAG (the Flags of D3D11_DEPTH_STENCIL_VIEW_DESC) */
#define D3D11_DSV_READ_ONLY_DEPTH 0x1u
#define D3D11_DSV_READ_ONLY_STENCIL 0x2u

#endif /* RT_WIN_D3D11VIEW_H */
