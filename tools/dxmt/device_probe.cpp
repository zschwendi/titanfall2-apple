// device_probe.cpp -- create a Direct3D 11 device with DXMT's native build and print what it reports.
// No window.  Exit status 0 if a device was created, 1 if not, 2 on usage errors.
//
// Build (done by tools/dxmt/probe.sh):
//   clang++ -std=c++20 -w -I third_party/dxmt/include/native/windows -I third_party/dxmt/include/native/directx \
//       tools/dxmt/device_probe.cpp -o <out>
// Run:    <out> third_party/dxmt/build-native
//
// Started from tools/scout/dxmt_probe.cpp (finding 35); adds feature level negotiation, CheckFeatureSupport,
// CheckFormatSupport, multisample counts and a minimal compute-shader creation.
#include "dxmt_load.h"
#include "dxbc_blobs.h"  // hand-assembled DXBC, see make_dxbc.py

#include <string.h>
#include <vector>

static const char *fl_name(D3D_FEATURE_LEVEL fl) {
  switch (fl) {
  case D3D_FEATURE_LEVEL_12_1: return "12_1";
  case D3D_FEATURE_LEVEL_12_0: return "12_0";
  case D3D_FEATURE_LEVEL_11_1: return "11_1";
  case D3D_FEATURE_LEVEL_11_0: return "11_0";
  case D3D_FEATURE_LEVEL_10_1: return "10_1";
  case D3D_FEATURE_LEVEL_10_0: return "10_0";
  case D3D_FEATURE_LEVEL_9_3: return "9_3";
  case D3D_FEATURE_LEVEL_9_2: return "9_2";
  case D3D_FEATURE_LEVEL_9_1: return "9_1";
  default: return "?";
  }
}

#define YN(x) ((x) ? "yes" : "no")

struct FmtBit { UINT bit; const char *name; };
static const FmtBit kFmtBits[] = {
  {D3D11_FORMAT_SUPPORT_BUFFER, "BUFFER"},
  {D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER, "IA_VB"},
  {D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER, "IA_IB"},
  {D3D11_FORMAT_SUPPORT_TEXTURE1D, "TEX1D"},
  {D3D11_FORMAT_SUPPORT_TEXTURE2D, "TEX2D"},
  {D3D11_FORMAT_SUPPORT_TEXTURE3D, "TEX3D"},
  {D3D11_FORMAT_SUPPORT_TEXTURECUBE, "CUBE"},
  {D3D11_FORMAT_SUPPORT_SHADER_LOAD, "LOAD"},
  {D3D11_FORMAT_SUPPORT_SHADER_SAMPLE, "SAMPLE"},
  {D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON, "SAMPLE_CMP"},
  {D3D11_FORMAT_SUPPORT_MIP, "MIP"},
  {D3D11_FORMAT_SUPPORT_MIP_AUTOGEN, "AUTOGEN"},
  {D3D11_FORMAT_SUPPORT_RENDER_TARGET, "RTV"},
  {D3D11_FORMAT_SUPPORT_BLENDABLE, "BLEND"},
  {D3D11_FORMAT_SUPPORT_DEPTH_STENCIL, "DSV"},
  {D3D11_FORMAT_SUPPORT_CPU_LOCKABLE, "CPU_LOCK"},
  {D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE, "MS_RESOLVE"},
  {D3D11_FORMAT_SUPPORT_DISPLAY, "DISPLAY"},
  {D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET, "MS_RTV"},
  {D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD, "MS_LOAD"},
  {D3D11_FORMAT_SUPPORT_SHADER_GATHER, "GATHER"},
  {D3D11_FORMAT_SUPPORT_BACK_BUFFER_CAST, "BB_CAST"},
  {D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW, "UAV_TYPED"},
};

static std::string fmt_bits(UINT v) {
  std::string s;
  for (auto &b : kFmtBits)
    if (v & b.bit) { if (!s.empty()) s += ","; s += b.name; }
  return s;
}

struct FmtEntry { DXGI_FORMAT f; const char *name; };
#define F(x) {DXGI_FORMAT_##x, #x}
static const FmtEntry kFormats[] = {
  F(R8G8B8A8_UNORM), F(R8G8B8A8_UNORM_SRGB), F(B8G8R8A8_UNORM), F(B8G8R8A8_UNORM_SRGB), F(R10G10B10A2_UNORM),
  F(R11G11B10_FLOAT), F(R16G16B16A16_FLOAT), F(R32G32B32A32_FLOAT), F(R32_FLOAT), F(R16_UNORM), F(R8_UNORM),
  F(D32_FLOAT), F(D24_UNORM_S8_UINT), F(D16_UNORM), F(D32_FLOAT_S8X24_UINT),
  F(BC1_UNORM), F(BC1_UNORM_SRGB), F(BC2_UNORM), F(BC2_UNORM_SRGB), F(BC3_UNORM), F(BC3_UNORM_SRGB),
  F(BC4_UNORM), F(BC4_SNORM), F(BC5_UNORM), F(BC5_SNORM), F(BC6H_UF16), F(BC6H_SF16), F(BC7_UNORM), F(BC7_UNORM_SRGB),
  F(R32_UINT), F(R32G32B32_FLOAT), F(R16G16_FLOAT),
};
#undef F

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <DXMT meson build dir>\n", argv[0]);
    return 2;
  }
  Dxmt dx;
  if (!dxmt_load(argv[1], &dx)) return 1;
  printf("dlopen: dxgi.dylib and d3d11.dylib loaded from %s\n", argv[1]);

  // --- 1. device with the default feature level list (what a caller that passes none gets) ---
  {
    ID3D11Device *d = nullptr; ID3D11DeviceContext *c = nullptr; D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
    HRESULT hr = dx.create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &fl, &c);
    printf("D3D11CreateDevice(default feature level list)   hr=0x%08x feature_level=%s (0x%04x)\n", (unsigned)hr, fl_name(fl), (unsigned)fl);
    if (c) c->Release();
    if (d) d->Release();
  }

  // --- 2. device with an explicit list, highest first (what the game will do) ---
  const D3D_FEATURE_LEVEL list[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  ID3D11Device *dev = nullptr; ID3D11DeviceContext *ctx = nullptr; D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = dx.create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, list, 4, D3D11_SDK_VERSION, &dev, &fl, &ctx);
  printf("D3D11CreateDevice(list 11_1..10_0, BGRA_SUPPORT) hr=0x%08x feature_level=%s (0x%04x)\n", (unsigned)hr, fl_name(fl), (unsigned)fl);
  if (FAILED(hr) || !dev || !ctx) return 1;
  printf("device->GetFeatureLevel() = %s; GetCreationFlags() = 0x%x; GetDeviceRemovedReason() = 0x%08x\n",
         fl_name(dev->GetFeatureLevel()), dev->GetCreationFlags(), (unsigned)dev->GetDeviceRemovedReason());
  printf("context type = %d (0 = immediate)\n", (int)ctx->GetType());

  // --- 3. adapter ---
  IDXGIDevice *dxgi_dev = nullptr;
  if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev))) {
    IDXGIAdapter *adapter = nullptr;
    if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter))) {
      DXGI_ADAPTER_DESC desc = {};
      if (SUCCEEDED(adapter->GetDesc(&desc))) {
        printf("adapter: \"%s\"\n", dxmt_utf16_to_utf8(desc.Description, sizeof(desc.Description) / 2).c_str());
        printf("  first 8 bytes of Description: ");
        for (int i = 0; i < 8; i++) printf("%02x ", ((unsigned char *)desc.Description)[i]);
        printf("(2-byte UTF-16 units packed from the start; wcslen as 4-byte wchar_t = %zu)\n", wcslen(desc.Description));
        printf("  VendorId=0x%04x DeviceId=0x%04x SubSysId=0x%x Revision=%u\n", desc.VendorId, desc.DeviceId, desc.SubSysId, desc.Revision);
        printf("  DedicatedVideoMemory=%llu DedicatedSystemMemory=%llu SharedSystemMemory=%llu\n", (unsigned long long)desc.DedicatedVideoMemory,
               (unsigned long long)desc.DedicatedSystemMemory, (unsigned long long)desc.SharedSystemMemory);
        printf("  AdapterLuid=%08x:%08x\n", (unsigned)desc.AdapterLuid.HighPart, (unsigned)desc.AdapterLuid.LowPart);
        printf("  sizeof(DXGI_ADAPTER_DESC) in this build = %zu (Windows x64: 304)\n", sizeof(DXGI_ADAPTER_DESC));
      }
      IDXGIAdapter1 *a1 = nullptr;
      if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter1), (void **)&a1))) {
        DXGI_ADAPTER_DESC1 d1 = {};
        if (SUCCEEDED(a1->GetDesc1(&d1))) printf("  IDXGIAdapter1::GetDesc1 Flags=0x%x (SOFTWARE=0x2)\n", d1.Flags);
        a1->Release();
      }
      IDXGIOutput *out = nullptr;
      HRESULT ho = adapter->EnumOutputs(0, &out);
      // Unpatched DXMT has no monitors in native mode (DXGI_ERROR_NOT_FOUND = 0x887a0002); with
      // patches/dxmt/0002 output 0 is the main CoreGraphics display.  tools/dxmt/window_test checks the outputs.
      printf("  EnumOutputs(0) hr=0x%08x (%s)\n", (unsigned)ho, out ? "an output exists" : "no outputs");
      if (out) out->Release();
      adapter->Release();
    }
    dxgi_dev->Release();
  }
  {
    IDXGIFactory1 *factory = nullptr;
    HRESULT h = dx.create_factory1(__uuidof(IDXGIFactory1), (void **)&factory);
    printf("CreateDXGIFactory1 hr=0x%08x\n", (unsigned)h);
    if (factory) {
      int n = 0;
      IDXGIAdapter1 *a = nullptr;
      while (factory->EnumAdapters1(n, &a) == S_OK) { a->Release(); n++; }
      printf("  EnumAdapters1 count = %d\n", n);
      factory->Release();
    }
  }

  // --- 4. CheckFeatureSupport ---
  printf("\n== CheckFeatureSupport ==\n");
  {
    D3D11_FEATURE_DATA_THREADING t = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_THREADING, &t, sizeof(t));
    printf("THREADING hr=0x%x DriverConcurrentCreates=%s DriverCommandLists=%s\n", (unsigned)hr, YN(t.DriverConcurrentCreates), YN(t.DriverCommandLists));
    D3D11_FEATURE_DATA_DOUBLES dd = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &dd, sizeof(dd));
    printf("DOUBLES hr=0x%x DoublePrecisionFloatShaderOps=%s\n", (unsigned)hr, YN(dd.DoublePrecisionFloatShaderOps));
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS x = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &x, sizeof(x));
    printf("D3D10_X_HARDWARE_OPTIONS hr=0x%x ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x=%s\n", (unsigned)hr, YN(x.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x));
    D3D11_FEATURE_DATA_D3D11_OPTIONS o = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o));
    printf("D3D11_OPTIONS hr=0x%x OutputMergerLogicOp=%s UAVOnlyRenderingForcedSampleCount=%s DiscardAPIsSeenByDriver=%s FlagsForUpdateAndCopySeenByDriver=%s\n"
           "  ClearView=%s CopyWithOverlap=%s ConstantBufferPartialUpdate=%s ConstantBufferOffsetting=%s MapNoOverwriteOnDynamicConstantBuffer=%s\n"
           "  MapNoOverwriteOnDynamicBufferSRV=%s MultisampleRTVWithForcedSampleCountOne=%s SAD4ShaderInstructions=%s ExtendedDoublesShaderInstructions=%s ExtendedResourceSharing=%s\n",
           (unsigned)hr, YN(o.OutputMergerLogicOp), YN(o.UAVOnlyRenderingForcedSampleCount), YN(o.DiscardAPIsSeenByDriver), YN(o.FlagsForUpdateAndCopySeenByDriver),
           YN(o.ClearView), YN(o.CopyWithOverlap), YN(o.ConstantBufferPartialUpdate), YN(o.ConstantBufferOffsetting), YN(o.MapNoOverwriteOnDynamicConstantBuffer),
           YN(o.MapNoOverwriteOnDynamicBufferSRV), YN(o.MultisampleRTVWithForcedSampleCountOne), YN(o.SAD4ShaderInstructions), YN(o.ExtendedDoublesShaderInstructions), YN(o.ExtendedResourceSharing));
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 o1 = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &o1, sizeof(o1));
    printf("D3D11_OPTIONS1 hr=0x%x TiledResourcesTier=%d MinMaxFiltering=%s ClearViewAlsoSupportsDepthOnlyFormats=%s MapOnDefaultBuffers=%s\n", (unsigned)hr,
           (int)o1.TiledResourcesTier, YN(o1.MinMaxFiltering), YN(o1.ClearViewAlsoSupportsDepthOnlyFormats), YN(o1.MapOnDefaultBuffers));
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 o2 = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &o2, sizeof(o2));
    printf("D3D11_OPTIONS2 hr=0x%x PSSpecifiedStencilRefSupported=%s TypedUAVLoadAdditionalFormats=%s ROVsSupported=%s ConservativeRasterizationTier=%d\n"
           "  MapOnDefaultTextures=%s StandardSwizzle=%s UnifiedMemoryArchitecture=%s\n", (unsigned)hr, YN(o2.PSSpecifiedStencilRefSupported),
           YN(o2.TypedUAVLoadAdditionalFormats), YN(o2.ROVsSupported), (int)o2.ConservativeRasterizationTier, YN(o2.MapOnDefaultTextures), YN(o2.StandardSwizzle), YN(o2.UnifiedMemoryArchitecture));
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 o3 = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &o3, sizeof(o3));
    printf("D3D11_OPTIONS3 hr=0x%x VPAndRTArrayIndexFromAnyShaderFeedingRasterizer=%s\n", (unsigned)hr, YN(o3.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer));
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO ai = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO, &ai, sizeof(ai));
    printf("ARCHITECTURE_INFO hr=0x%x TileBasedDeferredRenderer=%s\n", (unsigned)hr, YN(ai.TileBasedDeferredRenderer));
    D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT mp = {};
    hr = dev->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &mp, sizeof(mp));
    printf("SHADER_MIN_PRECISION_SUPPORT hr=0x%x PixelShader=0x%x AllOtherShaderStages=0x%x\n", (unsigned)hr, mp.PixelShaderMinPrecision, mp.AllOtherShaderStagesMinPrecision);
  }

  // --- 5. CheckFormatSupport ---
  printf("\n== CheckFormatSupport (ID3D11Device::CheckFormatSupport) ==\n");
  for (auto &e : kFormats) {
    UINT s = 0;
    hr = dev->CheckFormatSupport(e.f, &s);
    printf("%-22s hr=0x%08x 0x%08x %s\n", e.name, (unsigned)hr, s, FAILED(hr) ? "" : fmt_bits(s).c_str());
  }

  // --- 6. multisample ---
  printf("\n== CheckMultisampleQualityLevels (quality levels for sample counts 1 2 4 8 16) ==\n");
  const FmtEntry msfmts[] = {{DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM"}, {DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM"},
                             {DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT"}, {DXGI_FORMAT_D32_FLOAT, "D32_FLOAT"},
                             {DXGI_FORMAT_D24_UNORM_S8_UINT, "D24_UNORM_S8_UINT"}};
  for (auto &e : msfmts) {
    printf("%-22s", e.name);
    for (UINT n : {1u, 2u, 4u, 8u, 16u}) {
      UINT q = 0xdead;
      hr = dev->CheckMultisampleQualityLevels(e.f, n, &q);
      printf("  x%u: %s", n, FAILED(hr) ? "hr-fail" : std::to_string(q).c_str());
    }
    printf("\n");
  }

  // --- 7. compute: an empty cs_5_0, then a cs_5_0 that stores 0x1234abcd into a raw UAV buffer; read it back ---
  printf("\n== Compute ==\n");
  bool compute_ok = true;
  {
    ID3D11ComputeShader *cs = nullptr;
    hr = dev->CreateComputeShader(kCsEmptyDxbc, sizeof(kCsEmptyDxbc), nullptr, &cs);
    printf("CreateComputeShader(cs_5_0, empty main) hr=0x%08x\n", (unsigned)hr);
    if (cs) {
      ctx->CSSetShader(cs, nullptr, 0);
      ctx->Dispatch(1, 1, 1);
      ctx->Flush();
      printf("  CSSetShader + Dispatch(1,1,1) + Flush done\n");
      cs->Release();
    }
    compute_ok &= SUCCEEDED(hr);

    hr = dev->CreateComputeShader(kCsStoreDxbc, sizeof(kCsStoreDxbc), nullptr, &cs);
    printf("CreateComputeShader(cs_5_0, store_raw to u0) hr=0x%08x\n", (unsigned)hr);
    compute_ok &= SUCCEEDED(hr);
    if (cs) {
      D3D11_BUFFER_DESC bd = {};
      bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
      bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
      ID3D11Buffer *buf = nullptr;
      const UINT zero[4] = {0xdddddddd, 0xdddddddd, 0xdddddddd, 0xdddddddd};
      D3D11_SUBRESOURCE_DATA init = {zero, 16, 16};
      HRESULT h1 = dev->CreateBuffer(&bd, &init, &buf);
      D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
      ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = 4; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
      ID3D11UnorderedAccessView *uav = nullptr;
      HRESULT h2 = buf ? dev->CreateUnorderedAccessView(buf, &ud, &uav) : E_FAIL;
      bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      ID3D11Buffer *stg = nullptr;
      HRESULT h3 = dev->CreateBuffer(&bd, nullptr, &stg);
      printf("  CreateBuffer(raw UAV) hr=0x%x CreateUnorderedAccessView(RAW) hr=0x%x CreateBuffer(staging) hr=0x%x\n", (unsigned)h1, (unsigned)h2, (unsigned)h3);
      if (uav && stg) {
        ctx->CSSetShader(cs, nullptr, 0);
        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        ctx->Dispatch(1, 1, 1);
        ctx->CopyResource(stg, buf);
        D3D11_MAPPED_SUBRESOURCE m = {};
        HRESULT hm = ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m);
        if (SUCCEEDED(hm)) {
          const UINT *v = (const UINT *)m.pData;
          bool ok = v[0] == 0x1234abcd && v[1] == 0xdddddddd;
          printf("  readback after Dispatch(1,1,1): [0]=0x%08x (expect 0x1234abcd) [1]=0x%08x (expect untouched 0xdddddddd) -> %s\n", v[0], v[1], ok ? "PASS" : "FAIL");
          compute_ok &= ok;
          ctx->Unmap(stg, 0);
        } else {
          printf("  Map(staging) hr=0x%x\n", (unsigned)hm);
          compute_ok = false;
        }
      } else {
        compute_ok = false;
      }
      if (uav) uav->Release();
      if (stg) stg->Release();
      if (buf) buf->Release();
      cs->Release();
    }
  }
  printf("compute smoke test: %s\n", compute_ok ? "PASS" : "FAIL");

  ctx->Release();
  dev->Release();
  printf("\nDEVICE_PROBE_OK compute=%s\n", compute_ok ? "pass" : "FAIL");
  return 0;
}
