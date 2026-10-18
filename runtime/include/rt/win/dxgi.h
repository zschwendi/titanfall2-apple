/* DXGI: the structures and constants the do-nothing device's bodies and tests use (dxgi.h,
 * dxgitype.h, dxgicommon.h, dxgiformat.h).  Planner-owned; rt/win/d3d11.h includes it.
 *
 * Rules (docs/RUNTIME.md 3.3): fixed widths, no bit-fields, a _Static_assert on the size of
 * every structure and on the offset of every field.  An enumeration is a UINT (the interface
 * descriptions declare every enumeration as uint32_t: build/gen/com_types.h); its enumerators
 * are #defines with a `u` suffix.  A structure is `struct NAME { ... };` followed by
 * `typedef struct NAME NAME;` (the generated header has the same typedef for the names a
 * method uses; repeating a typedef is allowed).
 *
 * WHERE EVERY NUMBER COMES FROM: the MinGW-w64 header of that name in
 * third_party/dxmt/include/native/directx (the source of the interface descriptions too),
 * read for the 64-bit Windows layout.  `make d3d-layout-check` compiles those headers and
 * compares every size, offset and constant of this file with them.
 */
#ifndef RT_WIN_DXGI_H
#define RT_WIN_DXGI_H

#include <stddef.h>

#include "rt/wintypes.h"
#include "rt/win/base.h"            /* RECT */

/* dxgicommon.h */
struct DXGI_RATIONAL { UINT Numerator; UINT Denominator; };
typedef struct DXGI_RATIONAL DXGI_RATIONAL;
_Static_assert(sizeof(struct DXGI_RATIONAL) == 8, "");
_Static_assert(offsetof(struct DXGI_RATIONAL, Numerator) == 0, "");
_Static_assert(offsetof(struct DXGI_RATIONAL, Denominator) == 4, "");

struct DXGI_SAMPLE_DESC { UINT Count; UINT Quality; };
typedef struct DXGI_SAMPLE_DESC DXGI_SAMPLE_DESC;
_Static_assert(sizeof(struct DXGI_SAMPLE_DESC) == 8, "");
_Static_assert(offsetof(struct DXGI_SAMPLE_DESC, Count) == 0, "");
_Static_assert(offsetof(struct DXGI_SAMPLE_DESC, Quality) == 4, "");

/* dxgitype.h */
struct DXGI_MODE_DESC { UINT Width; UINT Height; struct DXGI_RATIONAL RefreshRate; UINT Format; UINT ScanlineOrdering; UINT Scaling; };
typedef struct DXGI_MODE_DESC DXGI_MODE_DESC;
_Static_assert(sizeof(struct DXGI_MODE_DESC) == 28, "");
_Static_assert(offsetof(struct DXGI_MODE_DESC, Width) == 0, "");
_Static_assert(offsetof(struct DXGI_MODE_DESC, Height) == 4, "");
_Static_assert(offsetof(struct DXGI_MODE_DESC, RefreshRate) == 8, "");
_Static_assert(offsetof(struct DXGI_MODE_DESC, Format) == 16, "");
_Static_assert(offsetof(struct DXGI_MODE_DESC, ScanlineOrdering) == 20, "");
_Static_assert(offsetof(struct DXGI_MODE_DESC, Scaling) == 24, "");

struct DXGI_RGB { FLOAT Red; FLOAT Green; FLOAT Blue; };
typedef struct DXGI_RGB DXGI_RGB;
_Static_assert(sizeof(struct DXGI_RGB) == 12, "");
_Static_assert(offsetof(struct DXGI_RGB, Red) == 0, "");
_Static_assert(offsetof(struct DXGI_RGB, Green) == 4, "");
_Static_assert(offsetof(struct DXGI_RGB, Blue) == 8, "");

struct DXGI_GAMMA_CONTROL { struct DXGI_RGB Scale; struct DXGI_RGB Offset; struct DXGI_RGB GammaCurve[1025]; };
typedef struct DXGI_GAMMA_CONTROL DXGI_GAMMA_CONTROL;
_Static_assert(sizeof(struct DXGI_GAMMA_CONTROL) == 12324, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL, Scale) == 0, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL, Offset) == 12, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL, GammaCurve) == 24, "");

struct DXGI_GAMMA_CONTROL_CAPABILITIES { BOOL ScaleAndOffsetSupported; FLOAT MaxConvertedValue; FLOAT MinConvertedValue; UINT NumGammaControlPoints; FLOAT ControlPointPositions[1025]; };
typedef struct DXGI_GAMMA_CONTROL_CAPABILITIES DXGI_GAMMA_CONTROL_CAPABILITIES;
_Static_assert(sizeof(struct DXGI_GAMMA_CONTROL_CAPABILITIES) == 4116, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL_CAPABILITIES, ScaleAndOffsetSupported) == 0, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL_CAPABILITIES, MaxConvertedValue) == 4, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL_CAPABILITIES, MinConvertedValue) == 8, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL_CAPABILITIES, NumGammaControlPoints) == 12, "");
_Static_assert(offsetof(struct DXGI_GAMMA_CONTROL_CAPABILITIES, ControlPointPositions) == 16, "");

/* dxgi.h */
struct DXGI_ADAPTER_DESC { WCHAR Description[128]; UINT VendorId; UINT DeviceId; UINT SubSysId; UINT Revision; SIZE_T DedicatedVideoMemory; SIZE_T DedicatedSystemMemory; SIZE_T SharedSystemMemory; struct { DWORD LowPart; LONG HighPart; } AdapterLuid; };
typedef struct DXGI_ADAPTER_DESC DXGI_ADAPTER_DESC;
_Static_assert(sizeof(struct DXGI_ADAPTER_DESC) == 304, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, Description) == 0, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, VendorId) == 256, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, DeviceId) == 260, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, SubSysId) == 264, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, Revision) == 268, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, DedicatedVideoMemory) == 272, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, DedicatedSystemMemory) == 280, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, SharedSystemMemory) == 288, "");
_Static_assert(offsetof(struct DXGI_ADAPTER_DESC, AdapterLuid) == 296, "");

struct DXGI_OUTPUT_DESC { WCHAR DeviceName[32]; RECT DesktopCoordinates; BOOL AttachedToDesktop; UINT Rotation; HMONITOR Monitor; };
typedef struct DXGI_OUTPUT_DESC DXGI_OUTPUT_DESC;
_Static_assert(sizeof(struct DXGI_OUTPUT_DESC) == 96, "");
_Static_assert(offsetof(struct DXGI_OUTPUT_DESC, DeviceName) == 0, "");
_Static_assert(offsetof(struct DXGI_OUTPUT_DESC, DesktopCoordinates) == 64, "");
_Static_assert(offsetof(struct DXGI_OUTPUT_DESC, AttachedToDesktop) == 80, "");
_Static_assert(offsetof(struct DXGI_OUTPUT_DESC, Rotation) == 84, "");
_Static_assert(offsetof(struct DXGI_OUTPUT_DESC, Monitor) == 88, "");

struct DXGI_SWAP_CHAIN_DESC { struct DXGI_MODE_DESC BufferDesc; struct DXGI_SAMPLE_DESC SampleDesc; UINT BufferUsage; UINT BufferCount; HWND OutputWindow; BOOL Windowed; UINT SwapEffect; UINT Flags; };
typedef struct DXGI_SWAP_CHAIN_DESC DXGI_SWAP_CHAIN_DESC;
_Static_assert(sizeof(struct DXGI_SWAP_CHAIN_DESC) == 72, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, BufferDesc) == 0, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, SampleDesc) == 28, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, BufferUsage) == 36, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, BufferCount) == 40, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, OutputWindow) == 48, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, Windowed) == 56, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, SwapEffect) == 60, "");
_Static_assert(offsetof(struct DXGI_SWAP_CHAIN_DESC, Flags) == 64, "");

struct DXGI_SURFACE_DESC { UINT Width; UINT Height; UINT Format; struct DXGI_SAMPLE_DESC SampleDesc; };
typedef struct DXGI_SURFACE_DESC DXGI_SURFACE_DESC;
_Static_assert(sizeof(struct DXGI_SURFACE_DESC) == 20, "");
_Static_assert(offsetof(struct DXGI_SURFACE_DESC, Width) == 0, "");
_Static_assert(offsetof(struct DXGI_SURFACE_DESC, Height) == 4, "");
_Static_assert(offsetof(struct DXGI_SURFACE_DESC, Format) == 8, "");
_Static_assert(offsetof(struct DXGI_SURFACE_DESC, SampleDesc) == 12, "");

/* DXGI_FORMAT (dxgiformat.h) */
#define DXGI_FORMAT_UNKNOWN 0x0u
#define DXGI_FORMAT_R32G32B32A32_TYPELESS 0x1u
#define DXGI_FORMAT_R32G32B32A32_FLOAT 0x2u
#define DXGI_FORMAT_R32G32B32A32_UINT 0x3u
#define DXGI_FORMAT_R32G32B32A32_SINT 0x4u
#define DXGI_FORMAT_R32G32B32_TYPELESS 0x5u
#define DXGI_FORMAT_R32G32B32_FLOAT 0x6u
#define DXGI_FORMAT_R32G32B32_UINT 0x7u
#define DXGI_FORMAT_R32G32B32_SINT 0x8u
#define DXGI_FORMAT_R16G16B16A16_TYPELESS 0x9u
#define DXGI_FORMAT_R16G16B16A16_FLOAT 0xau
#define DXGI_FORMAT_R16G16B16A16_UNORM 0xbu
#define DXGI_FORMAT_R16G16B16A16_UINT 0xcu
#define DXGI_FORMAT_R16G16B16A16_SNORM 0xdu
#define DXGI_FORMAT_R16G16B16A16_SINT 0xeu
#define DXGI_FORMAT_R32G32_TYPELESS 0xfu
#define DXGI_FORMAT_R32G32_FLOAT 0x10u
#define DXGI_FORMAT_R32G32_UINT 0x11u
#define DXGI_FORMAT_R32G32_SINT 0x12u
#define DXGI_FORMAT_R32G8X24_TYPELESS 0x13u
#define DXGI_FORMAT_D32_FLOAT_S8X24_UINT 0x14u
#define DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS 0x15u
#define DXGI_FORMAT_X32_TYPELESS_G8X24_UINT 0x16u
#define DXGI_FORMAT_R10G10B10A2_TYPELESS 0x17u
#define DXGI_FORMAT_R10G10B10A2_UNORM 0x18u
#define DXGI_FORMAT_R10G10B10A2_UINT 0x19u
#define DXGI_FORMAT_R11G11B10_FLOAT 0x1au
#define DXGI_FORMAT_R8G8B8A8_TYPELESS 0x1bu
#define DXGI_FORMAT_R8G8B8A8_UNORM 0x1cu
#define DXGI_FORMAT_R8G8B8A8_UNORM_SRGB 0x1du
#define DXGI_FORMAT_R8G8B8A8_UINT 0x1eu
#define DXGI_FORMAT_R8G8B8A8_SNORM 0x1fu
#define DXGI_FORMAT_R8G8B8A8_SINT 0x20u
#define DXGI_FORMAT_R16G16_TYPELESS 0x21u
#define DXGI_FORMAT_R16G16_FLOAT 0x22u
#define DXGI_FORMAT_R16G16_UNORM 0x23u
#define DXGI_FORMAT_R16G16_UINT 0x24u
#define DXGI_FORMAT_R16G16_SNORM 0x25u
#define DXGI_FORMAT_R16G16_SINT 0x26u
#define DXGI_FORMAT_R32_TYPELESS 0x27u
#define DXGI_FORMAT_D32_FLOAT 0x28u
#define DXGI_FORMAT_R32_FLOAT 0x29u
#define DXGI_FORMAT_R32_UINT 0x2au
#define DXGI_FORMAT_R32_SINT 0x2bu
#define DXGI_FORMAT_R24G8_TYPELESS 0x2cu
#define DXGI_FORMAT_D24_UNORM_S8_UINT 0x2du
#define DXGI_FORMAT_R24_UNORM_X8_TYPELESS 0x2eu
#define DXGI_FORMAT_X24_TYPELESS_G8_UINT 0x2fu
#define DXGI_FORMAT_R8G8_TYPELESS 0x30u
#define DXGI_FORMAT_R8G8_UNORM 0x31u
#define DXGI_FORMAT_R8G8_UINT 0x32u
#define DXGI_FORMAT_R8G8_SNORM 0x33u
#define DXGI_FORMAT_R8G8_SINT 0x34u
#define DXGI_FORMAT_R16_TYPELESS 0x35u
#define DXGI_FORMAT_R16_FLOAT 0x36u
#define DXGI_FORMAT_D16_UNORM 0x37u
#define DXGI_FORMAT_R16_UNORM 0x38u
#define DXGI_FORMAT_R16_UINT 0x39u
#define DXGI_FORMAT_R16_SNORM 0x3au
#define DXGI_FORMAT_R16_SINT 0x3bu
#define DXGI_FORMAT_R8_TYPELESS 0x3cu
#define DXGI_FORMAT_R8_UNORM 0x3du
#define DXGI_FORMAT_R8_UINT 0x3eu
#define DXGI_FORMAT_R8_SNORM 0x3fu
#define DXGI_FORMAT_R8_SINT 0x40u
#define DXGI_FORMAT_A8_UNORM 0x41u
#define DXGI_FORMAT_R1_UNORM 0x42u
#define DXGI_FORMAT_R9G9B9E5_SHAREDEXP 0x43u
#define DXGI_FORMAT_R8G8_B8G8_UNORM 0x44u
#define DXGI_FORMAT_G8R8_G8B8_UNORM 0x45u
#define DXGI_FORMAT_BC1_TYPELESS 0x46u
#define DXGI_FORMAT_BC1_UNORM 0x47u
#define DXGI_FORMAT_BC1_UNORM_SRGB 0x48u
#define DXGI_FORMAT_BC2_TYPELESS 0x49u
#define DXGI_FORMAT_BC2_UNORM 0x4au
#define DXGI_FORMAT_BC2_UNORM_SRGB 0x4bu
#define DXGI_FORMAT_BC3_TYPELESS 0x4cu
#define DXGI_FORMAT_BC3_UNORM 0x4du
#define DXGI_FORMAT_BC3_UNORM_SRGB 0x4eu
#define DXGI_FORMAT_BC4_TYPELESS 0x4fu
#define DXGI_FORMAT_BC4_UNORM 0x50u
#define DXGI_FORMAT_BC4_SNORM 0x51u
#define DXGI_FORMAT_BC5_TYPELESS 0x52u
#define DXGI_FORMAT_BC5_UNORM 0x53u
#define DXGI_FORMAT_BC5_SNORM 0x54u
#define DXGI_FORMAT_B5G6R5_UNORM 0x55u
#define DXGI_FORMAT_B5G5R5A1_UNORM 0x56u
#define DXGI_FORMAT_B8G8R8A8_UNORM 0x57u
#define DXGI_FORMAT_B8G8R8X8_UNORM 0x58u
#define DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM 0x59u
#define DXGI_FORMAT_B8G8R8A8_TYPELESS 0x5au
#define DXGI_FORMAT_B8G8R8A8_UNORM_SRGB 0x5bu
#define DXGI_FORMAT_B8G8R8X8_TYPELESS 0x5cu
#define DXGI_FORMAT_B8G8R8X8_UNORM_SRGB 0x5du
#define DXGI_FORMAT_BC6H_TYPELESS 0x5eu
#define DXGI_FORMAT_BC6H_UF16 0x5fu
#define DXGI_FORMAT_BC6H_SF16 0x60u
#define DXGI_FORMAT_BC7_TYPELESS 0x61u
#define DXGI_FORMAT_BC7_UNORM 0x62u
#define DXGI_FORMAT_BC7_UNORM_SRGB 0x63u
#define DXGI_FORMAT_AYUV 0x64u
#define DXGI_FORMAT_Y410 0x65u
#define DXGI_FORMAT_Y416 0x66u
#define DXGI_FORMAT_NV12 0x67u
#define DXGI_FORMAT_P010 0x68u
#define DXGI_FORMAT_P016 0x69u
#define DXGI_FORMAT_420_OPAQUE 0x6au
#define DXGI_FORMAT_YUY2 0x6bu
#define DXGI_FORMAT_Y210 0x6cu
#define DXGI_FORMAT_Y216 0x6du
#define DXGI_FORMAT_NV11 0x6eu
#define DXGI_FORMAT_AI44 0x6fu
#define DXGI_FORMAT_IA44 0x70u
#define DXGI_FORMAT_P8 0x71u
#define DXGI_FORMAT_A8P8 0x72u
#define DXGI_FORMAT_B4G4R4A4_UNORM 0x73u
#define DXGI_FORMAT_P208 0x82u
#define DXGI_FORMAT_V208 0x83u
#define DXGI_FORMAT_V408 0x84u

/* DXGI_MODE_SCANLINE_ORDER (dxgitype.h) */
#define DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED 0x0u
#define DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE 0x1u
#define DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST 0x2u
#define DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST 0x3u

/* DXGI_MODE_SCALING (dxgitype.h) */
#define DXGI_MODE_SCALING_UNSPECIFIED 0x0u
#define DXGI_MODE_SCALING_CENTERED 0x1u
#define DXGI_MODE_SCALING_STRETCHED 0x2u

/* DXGI_MODE_ROTATION (dxgitype.h) */
#define DXGI_MODE_ROTATION_UNSPECIFIED 0x0u
#define DXGI_MODE_ROTATION_IDENTITY 0x1u
#define DXGI_MODE_ROTATION_ROTATE90 0x2u
#define DXGI_MODE_ROTATION_ROTATE180 0x3u
#define DXGI_MODE_ROTATION_ROTATE270 0x4u

/* DXGI_SWAP_EFFECT (dxgi.h) */
#define DXGI_SWAP_EFFECT_DISCARD 0u
#define DXGI_SWAP_EFFECT_SEQUENTIAL 1u

/* DXGI_SWAP_CHAIN_FLAG (dxgi.h) */
#define DXGI_SWAP_CHAIN_FLAG_NONPREROTATED 0x1u
#define DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH 0x2u
#define DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE 0x4u

/* DXGI_USAGE (dxgi.h) */
#define DXGI_USAGE_SHADER_INPUT 0x10u
#define DXGI_USAGE_RENDER_TARGET_OUTPUT 0x20u
#define DXGI_USAGE_BACK_BUFFER 0x40u
#define DXGI_USAGE_SHARED 0x80u
#define DXGI_USAGE_READ_ONLY 0x100u
#define DXGI_USAGE_DISCARD_ON_PRESENT 0x200u
#define DXGI_USAGE_UNORDERED_ACCESS 0x400u

/* DXGI_ENUM_MODES (dxgi.h) */
#define DXGI_ENUM_MODES_INTERLACED 1u
#define DXGI_ENUM_MODES_SCALING 2u

/* DXGI_MWA (dxgi.h) */
#define DXGI_MWA_NO_WINDOW_CHANGES 0x1u
#define DXGI_MWA_NO_ALT_ENTER 0x2u
#define DXGI_MWA_NO_PRINT_SCREEN 0x4u
#define DXGI_MWA_VALID 0x7u

/* DXGI_PRESENT (dxgi.h) */
#define DXGI_PRESENT_TEST 0x00000001u
#define DXGI_PRESENT_DO_NOT_SEQUENCE 0x00000002u
#define DXGI_PRESENT_RESTART 0x00000004u

/* DXGI_MAX_SWAP_CHAIN_BUFFERS (dxgi.h) */
#define DXGI_MAX_SWAP_CHAIN_BUFFERS 16u

/* DXGI result codes (windows/windows_base.h) */
#define DXGI_ERROR_INVALID_CALL ((HRESULT)0x887a0001u)
#define DXGI_ERROR_NOT_FOUND ((HRESULT)0x887a0002u)
#define DXGI_ERROR_MORE_DATA ((HRESULT)0x887a0003u)
#define DXGI_ERROR_UNSUPPORTED ((HRESULT)0x887a0004u)
#define DXGI_ERROR_DEVICE_REMOVED ((HRESULT)0x887a0005u)
#define DXGI_ERROR_DEVICE_HUNG ((HRESULT)0x887a0006u)
#define DXGI_ERROR_DEVICE_RESET ((HRESULT)0x887a0007u)
#define DXGI_ERROR_WAS_STILL_DRAWING ((HRESULT)0x887a000au)
#define DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ((HRESULT)0x887a0022u)
#define DXGI_STATUS_OCCLUDED ((HRESULT)0x087a0001u)

#endif /* RT_WIN_DXGI_H */
