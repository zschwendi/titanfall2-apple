// window_probe.cpp -- what patched native DXMT does when asked for a swap chain on something that is not a window.
//
// Unpatched DXMT aborts the process for any non-null HWND (docs/findings/35-dxmt-build.md, section 6).  With
// patches/dxmt/0001 an HWND is a pointer to a DXMTNativeWindow; one that fails the magic/version/layer check is
// refused with DXGI_ERROR_INVALID_CALL.  This goes through D3D11CreateDeviceAndSwapChain (the window test uses
// IDXGIFactory::CreateSwapChain).  Each attempt runs in a forked child that loads DXMT itself, so that an abort()
// would only kill the child; the parent checks how each child ended.  No window is opened.
//
// Not tried, on purpose: an HWND that points at unmapped memory (for example 0x1000).  That is a bad pointer and
// crashes, like a bad pointer handed to any C function; the caller has to hand DXMT a DXMTNativeWindow or null.
//
// Build:  clang++ -std=c++20 -w -I third_party/dxmt/include/native/windows -I third_party/dxmt/include/native/directx \
//             -I third_party/dxmt/include/native tools/dxmt/window_probe.cpp -o <out>
// Run:    <out> third_party/dxmt/build-native      (tools/dxmt/window.sh does both)
// Exit:   0 and a last line WINDOW_PROBE_OK if every attempt failed with an error code and no child died of a signal.
#include "dxmt_load.h"

#include "dxmt_native_window.h"

#include <signal.h>
#include <string.h>
#include <sys/wait.h>

static int child(const std::string &dir, void *hwnd) {
  Dxmt dx;
  if (!dxmt_load(dir, &dx)) return 2;
  auto create = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)dlsym(dx.d3d11, "D3D11CreateDeviceAndSwapChain");
  if (!create) { fprintf(stderr, "no D3D11CreateDeviceAndSwapChain\n"); return 2; }
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferDesc.Width = 640; sd.BufferDesc.Height = 480; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
  sd.OutputWindow = (HWND)hwnd; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  ID3D11Device *dev = nullptr; ID3D11DeviceContext *ctx = nullptr; IDXGISwapChain *sc = nullptr; D3D_FEATURE_LEVEL fl;
  HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
  fprintf(stderr, "child: D3D11CreateDeviceAndSwapChain returned hr=0x%08x swapchain=%p device=%p\n", (unsigned)hr, (void *)sc, (void *)dev);
  if (SUCCEEDED(hr) || sc) return 0;                        // must not happen
  return hr == DXGI_ERROR_INVALID_CALL ? 10 : 11;           // 10 = refused the way we expect
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <DXMT meson build dir>\n", argv[0]); return 2; }
  std::string dir = argv[1];

  static int not_a_layer = 0;
  DXMTNativeWindow wrong_magic = {0x12345678, DXMT_NATIVE_WINDOW_VERSION, &not_a_layer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  DXMTNativeWindow wrong_version = {DXMT_NATIVE_WINDOW_MAGIC, 99, &not_a_layer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  DXMTNativeWindow no_layer = {DXMT_NATIVE_WINDOW_MAGIC, DXMT_NATIVE_WINDOW_VERSION, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  uint64_t zeros[16] = {};
  const struct { const char *what; void *hwnd; } cases[] = {
    {"null HWND", nullptr},
    {"64 zero bytes", zeros},
    {"DXMTNativeWindow with a wrong magic number", &wrong_magic},
    {"DXMTNativeWindow with an unknown version", &wrong_version},
    {"DXMTNativeWindow without a layer", &no_layer},
  };
  int bad = 0;
  for (auto &c : cases) {
    fflush(stdout); fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) _exit(child(dir, c.hwnd));
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) {
      printf("%-45s child killed by signal %d (%s)  <-- FAIL\n", c.what, WTERMSIG(st), strsignal(WTERMSIG(st)));
      bad++;
    } else if (WEXITSTATUS(st) == 10) {
      printf("%-45s refused with DXGI_ERROR_INVALID_CALL, no abort\n", c.what);
    } else {
      printf("%-45s child exited with status %d (0 = a swap chain was created, 11 = another error code, 2 = setup)  <-- FAIL\n", c.what, WEXITSTATUS(st));
      bad++;
    }
  }
  if (bad) { printf("WINDOW_PROBE_FAIL\n"); return 1; }
  printf("WINDOW_PROBE_OK\n");
  return 0;
}
