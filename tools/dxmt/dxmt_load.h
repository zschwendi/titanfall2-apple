// dxmt_load.h -- load DXMT's native libraries from a meson build tree and resolve the entry points we use.
//
// Shared by device_probe.cpp and offscreen_triangle.cpp.  The libraries are opened with dlopen by path
// (RTLD_NOW | RTLD_GLOBAL); d3d11.dylib and dxgi.dylib name winemetal.dylib and each other as
// @rpath/<name>.dylib and carry build-tree rpaths (@loader_path/../nativemetal, ../dxgi, ...), so opening
// them from <build>/src/<lib>/ is enough: no DYLD_* variable is needed.
//
// Headers: DXMT's native Windows-type shims and the mingw-w64 Direct3D headers, i.e.
//   -I third_party/dxmt/include/native/windows -I third_party/dxmt/include/native/directx
#pragma once

#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

typedef HRESULT (*PFN_CREATE_DXGI_FACTORY1)(const IID &, void **);

struct Dxmt {
  void *dxgi = nullptr;
  void *d3d11 = nullptr;
  PFN_D3D11_CREATE_DEVICE create_device = nullptr;
  PFN_CREATE_DXGI_FACTORY1 create_factory1 = nullptr;
};

// dir: the meson build directory (third_party/dxmt/build-native), or any directory that holds
// dxgi.dylib, d3d11.dylib and winemetal.dylib side by side.
static inline bool dxmt_load(const std::string &dir, Dxmt *out) {
  std::string dxgi_path = dir + "/src/dxgi/dxgi.dylib";
  std::string d3d11_path = dir + "/src/d3d11/d3d11.dylib";
  if (access(dxgi_path.c_str(), R_OK) != 0) {
    dxgi_path = dir + "/dxgi.dylib";
    d3d11_path = dir + "/d3d11.dylib";
  }
  if (access(dxgi_path.c_str(), R_OK) != 0) {
    dxgi_path = dir + "/dxgi.framework/dxgi";
    d3d11_path = dir + "/d3d11.framework/d3d11";
  }
  out->dxgi = dlopen(dxgi_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (!out->dxgi) {
    fprintf(stderr, "dlopen %s: %s\n", dxgi_path.c_str(), dlerror());
    return false;
  }
  out->d3d11 = dlopen(d3d11_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (!out->d3d11) {
    fprintf(stderr, "dlopen %s: %s\n", d3d11_path.c_str(), dlerror());
    return false;
  }
  out->create_device = (PFN_D3D11_CREATE_DEVICE)dlsym(out->d3d11, "D3D11CreateDevice");
  out->create_factory1 = (PFN_CREATE_DXGI_FACTORY1)dlsym(out->dxgi, "CreateDXGIFactory1");
  if (!out->create_device || !out->create_factory1) {
    fprintf(stderr, "missing export: D3D11CreateDevice=%p CreateDXGIFactory1=%p\n", (void *)out->create_device,
            (void *)out->create_factory1);
    return false;
  }
  return true;
}

// DXGI_ADAPTER_DESC::Description in this build: DXMT writes 2-byte UTF-16 units packed from the start of a
// buffer the native header types as wchar_t[128] (4-byte).  Decode the 2-byte reading to UTF-8 (ASCII/BMP).
static inline std::string dxmt_utf16_to_utf8(const void *buf, size_t max_units) {
  const char16_t *s = (const char16_t *)buf;
  std::string out;
  for (size_t i = 0; i < max_units && s[i]; i++) {
    char16_t c = s[i];
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
    else { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
  }
  return out;
}
