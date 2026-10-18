// format_probe.cpp -- what DXMT's device on this Mac answers to ID3D11Device::CheckFormatSupport, for
// every DXGI_FORMAT value from 1 to 132.  Nothing else: one device, no swap chain, no resource, no
// shader, no draw.  It is the measurement behind rt_nulldev_format_support (runtime/nulldev/objects.c),
// which before it held the 32 formats device_probe.cpp asks (finding 35, section 3).
//
// Build:  clang++ -std=c++20 -w -I third_party/dxmt/include/native/windows \
//             -I third_party/dxmt/include/native/directx -I tools/dxmt tools/dxmt/format_probe.cpp -o <out>
// Run:    DXMT_LOG_PATH=none DXMT_SHADER_CACHE_PATH=<an empty directory> <out> third_party/dxmt/build-native
//
// The device is made as device_probe.cpp makes its second one (the list 11_1..10_0 with BGRA_SUPPORT),
// so that the 32 values it measured can be held against the same lines here.
// Output, one line per value:   <number> hr=0x<8 hex> 0x<8 hex>
#include "dxmt_load.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <DXMT meson build dir>\n", argv[0]);
    return 2;
  }
  Dxmt dx;
  if (!dxmt_load(argv[1], &dx)) return 1;
  const D3D_FEATURE_LEVEL list[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  ID3D11Device *dev = nullptr; ID3D11DeviceContext *ctx = nullptr; D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = dx.create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, list, 4,
                                D3D11_SDK_VERSION, &dev, &fl, &ctx);
  printf("D3D11CreateDevice(list 11_1..10_0, BGRA_SUPPORT) hr=0x%08x feature_level=0x%04x\n", (unsigned)hr, (unsigned)fl);
  if (FAILED(hr) || !dev || !ctx) return 1;
  for (unsigned f = 1; f <= 132; f++) {
    UINT s = 0xdeadbeef;
    hr = dev->CheckFormatSupport((DXGI_FORMAT)f, &s);
    printf("%u hr=0x%08x 0x%08x\n", f, (unsigned)hr, s);
  }
  ctx->Release();
  dev->Release();
  printf("FORMAT_PROBE_DONE\n");
  return 0;
}
