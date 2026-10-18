// load_probe.cpp -- do DXMT's three native libraries load from one directory and give a Direct3D 11 device,
// optionally on a thread that has the x86 memory-ordering mode on?  For step 0c of the picture track
// (docs/handoff/design-picture.md, section 5): the planner puts this program and the three libraries into the signed
// app bundle and runs it there.  Nothing of our runtime is used; no window; no game file.
//
//   load_probe [DIR] [--tso]
//
//   DIR     the directory that holds winemetal.dylib, dxgi.dylib and d3d11.dylib side by side.
//           Default: ../Frameworks beside the executable (Contents/Frameworks of an app bundle).
//   --tso   before anything else the working thread asks for the x86 memory-ordering mode,
//           thread_set_x86_64_compat(1), as experiments/tso/tso_litmus.c does.  The kernel grants it only to a
//           program signed with com.apple.developer.cross-architecture-support from a bundle with the profile
//           (experiments/tso/README.md); anywhere else it is refused and this program exits 3.
//
// What it does, all on one working thread (the main thread only waits: in the design the main thread is AppKit's
// and has the mode off, and the threads that call into DXMT have it on):
//   1. --tso: thread_set_x86_64_compat(1);
//   2. dlopen of DIR/winemetal.dylib, DIR/dxgi.dylib, DIR/d3d11.dylib by absolute path, in that order (dxgi needs
//      winemetal, d3d11 needs both; each finds the others through its own @loader_path rpath, patch 5 of the fork);
//   3. D3D11CreateDevice with no window, feature levels 11_1 then 11_0;
//   4. CreateBuffer (a 256-byte vertex buffer with initial data), Release; Flush; Release of context and device;
//   5. one line on standard output, beginning "load_probe: ok".
//
// Exit:  0 ok;  1 usage;  2 a library did not load or lacks an entry point (the message names it);
//        3 --tso and the mode was refused;  4 no device;  5 no buffer.
//
// Build:  clang++ -std=c++20 -w -I third_party/dxmt/include/native/windows -I third_party/dxmt/include/native/directx
//             tools/dxmt/load_probe.cpp -o build/dxmt/load_probe            (tools/dxmt/load_probe.sh does it)
#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/mach_traps.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <windows.h>
#include <d3d11.h>

struct Job {
  std::string dir;
  bool tso = false;
  int status = 0;
};

static void *work(void *arg) {
  Job *job = (Job *)arg;

  if (job->tso) {
    kern_return_t kr = thread_set_x86_64_compat(1);
    if (kr != KERN_SUCCESS) {
      printf("load_probe: x86 memory ordering REFUSED: thread_set_x86_64_compat(1) = %d (%s); nothing was loaded\n", (int)kr, mach_error_string(kr));
      job->status = 3;
      return nullptr;
    }
  }

  const char *names[3] = {"winemetal.dylib", "dxgi.dylib", "d3d11.dylib"};
  void *handles[3] = {};
  for (int i = 0; i < 3; i++) {
    std::string path = job->dir + "/" + names[i];
    handles[i] = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handles[i]) {
      printf("load_probe: dlopen failed for %s: %s\n", path.c_str(), dlerror());
      job->status = 2;
      return nullptr;
    }
  }
  auto create_device = (PFN_D3D11_CREATE_DEVICE)dlsym(handles[2], "D3D11CreateDevice");
  if (!create_device) {
    printf("load_probe: %s/d3d11.dylib has no D3D11CreateDevice\n", job->dir.c_str());
    job->status = 2;
    return nullptr;
  }
  // patch 4 of the fork: where the host registers its log and abort functions
  const bool hooks = dlsym(handles[0], "DXMTSetHostLogFunction") && dlsym(handles[0], "DXMTSetHostAbortFunction");

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL level = (D3D_FEATURE_LEVEL)0;
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  HRESULT hr = create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &device, &level, &context);
  if (FAILED(hr) || !device || !context) {
    printf("load_probe: D3D11CreateDevice failed: hr=0x%08x\n", (unsigned)hr);
    job->status = 4;
    return nullptr;
  }

  unsigned char bytes[256];
  for (int i = 0; i < 256; i++) bytes[i] = (unsigned char)i;
  D3D11_BUFFER_DESC bd = {};
  bd.ByteWidth = sizeof(bytes);
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA init = {};
  init.pSysMem = bytes;
  ID3D11Buffer *buffer = nullptr;
  hr = device->CreateBuffer(&bd, &init, &buffer);
  if (FAILED(hr) || !buffer) {
    printf("load_probe: CreateBuffer failed: hr=0x%08x\n", (unsigned)hr);
    job->status = 5;
    return nullptr;
  }
  D3D11_BUFFER_DESC got = {};
  buffer->GetDesc(&got);
  const unsigned long buffer_refs = buffer->Release();
  context->Flush();
  context->Release();
  const unsigned long device_refs = device->Release();

  printf("load_probe: ok dir=%s x86_ordering=%s libraries=3 feature_level=0x%04x buffer=%u bytes created and released (refs %lu) "
         "device released (refs %lu) host_hooks=%s\n",
         job->dir.c_str(), job->tso ? "on" : "not asked", (unsigned)level, got.ByteWidth, buffer_refs, device_refs, hooks ? "present" : "ABSENT");
  job->status = (got.ByteWidth == sizeof(bytes)) ? 0 : 5;
  return nullptr;
}

int main(int argc, char **argv) {
  Job job;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--tso")) job.tso = true;
    else if (argv[i][0] != '-' && job.dir.empty()) job.dir = argv[i];
    else { fprintf(stderr, "usage: %s [DIR] [--tso]\n", argv[0]); return 1; }
  }
  if (job.dir.empty()) {  // ../Frameworks beside the executable
    char exe[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) != 0 || !realpath(exe, real)) { fprintf(stderr, "load_probe: cannot find my own path\n"); return 1; }
    job.dir = std::string(dirname(real)) + "/../Frameworks";
  }
  char abs[PATH_MAX];
  if (!realpath(job.dir.c_str(), abs)) { printf("load_probe: no directory %s\n", job.dir.c_str()); return 2; }
  job.dir = abs;
  // DXMT's log file would land in the current directory, its shader cache in the user's cache directory: neither is wanted here
  setenv("DXMT_LOG_PATH", "none", 0);
  setenv("DXMT_SHADER_CACHE", "0", 0);
  setenv("DXMT_LOG_LEVEL", "error", 0);  // DXMT's 13 info and warning lines of device creation stay off standard error

  pthread_t thread;
  if (pthread_create(&thread, nullptr, work, &job) != 0) { fprintf(stderr, "load_probe: pthread_create failed\n"); return 1; }
  pthread_join(thread, nullptr);
  fflush(stdout);
  return job.status;
}
