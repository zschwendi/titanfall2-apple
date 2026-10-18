// dxmt_probe.cpp -- smallest native DXMT program: create a device, print adapter name and feature level.
//
// STATUS: written for finding 35, compile-checked, NOT RUN.  DXMT could not be built on this Mac
// (missing meson, ninja, LLVM 15, Metal Toolchain; see docs/findings/35-dxmt-native.md), so there are no
// d3d11.dylib / dxgi.dylib / winemetal.dylib to load.
//
// Build:  clang++ -std=c++20 -w -I third_party/dxmt/include/native/windows \
//             -I third_party/dxmt/include/native/directx tools/scout/dxmt_probe.cpp -o /tmp/dxmt_probe
// Run:    /tmp/dxmt_probe <directory holding d3d11.dylib, dxgi.dylib, winemetal.dylib>
//
// The libraries are opened with dlopen so this file has no link-time dependency on them.  They name
// each other as @rpath/<name>.dylib (meson's default on macOS; unverified), so the directory must also be
// on DYLD_LIBRARY_PATH unless the build tree's rpaths already point at it.
// No window is needed: D3D11CreateDevice -> CreateDXGIFactory1 -> EnumAdapters -> D3D11CoreCreateDevice
// never touches the window system (third_party/dxmt/src/d3d11/d3d11.cpp:113-205).
//
// Expected, from reading the source (all unverified until run):
//   * "adapter" is the MTLDevice name, e.g. "Apple M3 Pro".  DXMT writes it with
//     NSString getCString:encoding:NSUnicodeStringEncoding straight into DXGI_ADAPTER_DESC::Description
//     (third_party/dxmt/src/dxgi/dxgi_adapter.cpp:155-158), i.e. 2-byte UTF-16 units packed from the start of a
//     buffer that the native header types as wchar_t[128] (4-byte).  The probe therefore reads it as
//     char16_t and skips a byte-order mark if one is present; it reports whether there was one.
//   * IDXGIAdapter::EnumOutputs(0) returns DXGI_ERROR_NOT_FOUND in the headless window-system backend
//     (third_party/dxmt/src/util/wsi_monitor_headless.cpp:19-21).
//   * stderr gets "warn: D3DKMTOpenAdapterFromLuid not implemented." style lines (util_d3dkmt.h:575).
#include <dlfcn.h>
#include <stdio.h>
#include <string>

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

typedef HRESULT (*PFN_CREATE_DXGI_FACTORY1)(const IID &, void **);

static std::string utf16_to_utf8(const char16_t *s, size_t max, bool *had_bom) {
  std::string out;
  size_t i = 0;
  *had_bom = false;
  if (max && s[0] == 0xFEFF) {
    *had_bom = true;
    i = 1;
  }
  for (; i < max && s[i]; i++) {
    char16_t c = s[i];
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
    else { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
  }
  return out;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <dir with d3d11.dylib dxgi.dylib winemetal.dylib>\n", argv[0]);
    return 2;
  }
  std::string dir = argv[1];
  void *dxgi = dlopen((dir + "/dxgi.dylib").c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (!dxgi) { fprintf(stderr, "dlopen dxgi.dylib: %s\n", dlerror()); return 1; }
  void *d3d11 = dlopen((dir + "/d3d11.dylib").c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (!d3d11) { fprintf(stderr, "dlopen d3d11.dylib: %s\n", dlerror()); return 1; }

  auto create_device = (PFN_D3D11_CREATE_DEVICE)dlsym(d3d11, "D3D11CreateDevice");
  auto create_factory = (PFN_CREATE_DXGI_FACTORY1)dlsym(dxgi, "CreateDXGIFactory1");
  if (!create_device || !create_factory) {
    fprintf(stderr, "missing export: D3D11CreateDevice=%p CreateDXGIFactory1=%p\n", (void *)create_device, (void *)create_factory);
    return 1;
  }

  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
  printf("D3D11CreateDevice hr=0x%08x feature_level=0x%04x (%d.%d)\n", (unsigned)hr, (unsigned)fl, (fl >> 12) & 0xF, (fl >> 8) & 0xF);
  if (FAILED(hr)) return 1;

  IDXGIDevice *dxgi_dev = nullptr;
  if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev))) {
    IDXGIAdapter *adapter = nullptr;
    if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter))) {
      DXGI_ADAPTER_DESC desc = {};
      if (SUCCEEDED(adapter->GetDesc(&desc))) {
        bool bom;
        printf("adapter: \"%s\" bom=%d vendor=0x%04x dedicated_video_memory=%llu\n",
               utf16_to_utf8((const char16_t *)desc.Description, sizeof(desc.Description) / 2, &bom).c_str(), bom,
               desc.VendorId, (unsigned long long)desc.DedicatedVideoMemory);
      }
      IDXGIOutput *out = nullptr;
      HRESULT ho = adapter->EnumOutputs(0, &out);
      printf("EnumOutputs(0) hr=0x%08x\n", (unsigned)ho);
      if (out) out->Release();
      adapter->Release();
    }
    dxgi_dev->Release();
  }
  printf("sizeof(DXGI_ADAPTER_DESC) in this build = %zu (Windows x64: 304)\n", sizeof(DXGI_ADAPTER_DESC));

  IDXGIFactory1 *factory = nullptr;
  hr = create_factory(__uuidof(IDXGIFactory1), (void **)&factory);
  printf("CreateDXGIFactory1 hr=0x%08x\n", (unsigned)hr);
  if (factory) factory->Release();

  ctx->Release();
  dev->Release();
  return 0;
}
