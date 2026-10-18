// window_test.cpp -- a Direct3D 11 swap chain from DXMT's native build presenting into a CAMetalLayer that this
// program owns, in an SDL3 window.  The program checks itself; exit status 0 only if every check held.
// See docs/findings/35-dxmt-window.md.  Needs DXMT with patches/dxmt/*.patch (tools/dxmt/build.sh applies them).
//
// What it does (a few seconds with a window on screen; the window is not activated and takes no input focus):
//   outputs    IDXGIAdapter::EnumOutputs / GetDesc / GetDisplayModeList / FindClosestMatchingMode against what SDL
//              says about the same displays;
//   handles    IDXGIFactory::CreateSwapChain refuses a null window, a wrong magic number and a missing layer;
//   window     640x360, hidden until its layer exists; the layer is SDL_Metal_CreateView's (default) or a view of
//              our own (--own-layer); the HWND given to DXGI is a DXMTNativeWindow that points at the layer;
//   V          frames with Present(1, 0), each one checked in full (see below): a clear colour and a triangle
//              colour that step every frame;
//   A, A2, B   pacing: frames with Present(1, 0), Present(2, 0), Present(0, 0) and nothing else in the loop;
//   L          the Present(1, 0) loop again with IDXGIDevice1::SetMaximumFrameLatency(1), for the latency figure;
//   C          a minimized window (DXGI_STATUS_OCCLUDED, nothing presented), SetFullscreenState(TRUE) failing
//              cleanly, ResizeTarget reaching the host;
//   D          SDL_SetWindowSize(800, 450), then ResizeBuffers(0, 0): the size comes from the host;
//   E          ResizeBuffers(320, 180): a swap chain smaller than the window;
//   F          Present and ResizeBuffers from a second thread while the main thread first sleeps, then pumps SDL
//              events (DXMT sets layer properties with dispatch_sync to the main queue);
//   G          release the swap chain, create a second one (B8G8R8A8_UNORM, size 0x0 = ask the host), release it;
//   teardown   every Release, the view, the window; the layer must be deallocated.
// A frame "checked in full" (every frame of V, C, D, E, F, G): the back buffer is copied to a staging texture and
// read BEFORE Present (every pixel is the clear colour or the triangle colour, the triangle has the right area and
// is the right way up); after Present and a GPU sync point, the drawable that DXMT took from our layer is copied
// back with Metal and checked the same way (--no-probe turns the drawable part off).
// Pacing is checked twice.  Always: what DXMT asks Metal for with each drawable (presentDrawable:afterMinimumDuration:
// with SyncInterval / refresh rate, plain presentDrawable: for SyncInterval 0).  When Core Animation reports that the
// frames were shown (it does not while the display sleeps or the window is covered): the times it reports.
//
// Usage:  window_test <DXMT meson build dir> [--frames N] [--own-layer] [--hidpi] [--no-probe] [--no-pool] [--hidden]
//                     [--foreground] [--hold SECONDS] [--timeout SECONDS] [--format NAME] [--buffers N]
//   --format F    format of the first swap chain: R8G8B8A8_UNORM (default), R8G8B8A8_UNORM_SRGB, B8G8R8A8_UNORM,
//                 B8G8R8A8_UNORM_SRGB.  The game's is R8G8B8A8_UNORM_SRGB.  For an _SRGB format the bytes expected in
//                 the back buffer and in the drawable are the sRGB encoding of the colours drawn (see stored()).
//   --buffers N   BufferCount of the first swap chain (default 2; the game's is 3)
//   --hidden      never show the window: everything runs, Core Animation shows no frame (as with a sleeping display)
//   --max-drawables N  set the layer's maximumDrawableCount (2 or 3) before DXMT sees it, for the latency figures
//   --stale-size  an experiment, not a check: after E the window changes size and ResizeBuffers is NOT called; one
//                 frame is presented and the drawable DXMT got is described (docs/findings/35-dxmt-window.md)
//   --foreground  become a normal foreground app and bring the window to the front (for a human to look at)
//   --hold S      keep presenting for S more seconds before teardown (at most 20; for a human to look at)
//   --no-pool     run without autorelease pools on the threads that call DXMT (shows what then stays alive)
// Exit: 0 all checks passed, 1 a check failed, 2 setup failed, 3 watchdog timeout (the process kills itself, which
// also removes the window).
#include "dxmt_load.h"
#include "dxbc_blobs.h"
#include "window_test_layer.h"

#include "dxmt_native_window.h"  // -I third_party/dxmt/include/native

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <atomic>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <thread>
#include <vector>

// ---- checks -----------------------------------------------------------------------------------------------------------

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                                                               \
  do {                                                                                                                 \
    g_checks++;                                                                                                        \
    if (!(cond)) {                                                                                                     \
      g_fail++;                                                                                                        \
      printf("  FAIL ");                                                                                               \
      printf(__VA_ARGS__);                                                                                             \
      printf("\n");                                                                                                    \
    }                                                                                                                  \
  } while (0)

// a call that must return exactly S_OK
#define HR(expr)                                                                                                       \
  do {                                                                                                                 \
    HRESULT hr_ = (expr);                                                                                              \
    g_checks++;                                                                                                        \
    if (hr_ != S_OK) {                                                                                                 \
      g_fail++;                                                                                                        \
      printf("  FAIL %s returned 0x%08x\n", #expr, (unsigned)hr_);                                                     \
    }                                                                                                                  \
  } while (0)

static void sleep_ms(int ms) { SDL_Delay((Uint32)ms); }

// An autorelease pool for the calling thread; DXMT autoreleases Objective-C objects on the threads that call it.
static bool g_use_pools = true;
struct Pool {
  void *pool;
  Pool() : pool(g_use_pools ? wl_pool_push() : nullptr) {}
  ~Pool() { if (pool) wl_pool_pop(pool); }
  void drain() { if (pool) { wl_pool_pop(pool); pool = wl_pool_push(); } }
  Pool(const Pool &) = delete;
};

// ---- the host side: what our runtime would keep for a window -------------------------------------------------------------

struct Host {
  SDL_Window *window = nullptr;
  void *nswindow = nullptr;
  SDL_MetalView sdl_view = nullptr;
  void *own_view = nullptr;
  void *layer = nullptr;
  bool hidpi = false;
  DXMTNativeWindow handle = {};
  std::atomic<uint64_t> client_size{0};  // width << 32 | height, in swap chain pixels
  std::atomic<uint32_t> display_id{0};
  std::atomic<int> minimized{0};
  std::atomic<int> size_queries{0};
  std::atomic<uint64_t> requested_size{0};
  std::atomic<int> size_requests{0};
};

static void host_get_client_size(void *user, uint32_t *width, uint32_t *height) {
  Host *host = (Host *)user;
  uint64_t size = host->client_size.load();
  host->size_queries++;
  *width = (uint32_t)(size >> 32);
  *height = (uint32_t)size;
}
static uint32_t host_get_display_id(void *user) { return ((Host *)user)->display_id.load(); }
static int host_is_minimized(void *user) { return ((Host *)user)->minimized.load(); }
static void host_request_client_size(void *user, uint32_t width, uint32_t height) {
  Host *host = (Host *)user;
  host->requested_size = ((uint64_t)width << 32) | height;
  host->size_requests++;
}

// main thread only
static void host_refresh(Host *host) {
  int w = 0, h = 0;
  if (host->hidpi) SDL_GetWindowSizeInPixels(host->window, &w, &h);
  else SDL_GetWindowSize(host->window, &w, &h);
  host->client_size = ((uint64_t)(uint32_t)w << 32) | (uint32_t)h;
  host->display_id = wl_window_display_id(host->nswindow);
}

static bool g_quit_requested = false;
static void host_pump(Host *host) {
  SDL_PumpEvents();
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: g_quit_requested = true; break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_MOVED: host_refresh(host); break;
    case SDL_EVENT_WINDOW_MINIMIZED: host->minimized = 1; break;
    case SDL_EVENT_WINDOW_RESTORED: host->minimized = 0; break;
    default: break;
    }
  }
}

// ---- the pattern --------------------------------------------------------------------------------------------------------

struct Rgba { int r, g, b, a; };
static bool near(Rgba a, Rgba b, int tol) {
  return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol && abs(a.b - b.b) <= tol && abs(a.a - b.a) <= tol;
}
// both step every frame; they never come within the tolerance of each other (nor do their sRGB encodings: the
// largest channel difference is at least 46 as drawn and at least 28 encoded, frames 0 to 4999)
static Rgba clear_of(int f) { return {(f * 5) % 256, (f * 3 + 40) % 256, (f * 7 + 90) % 256, 255}; }
static Rgba tri_of(int f) { return {255 - (f * 5) % 256, 204, (f * 11) % 256, 255}; }

// What the bytes of a render target hold after colour c was written to it (cleared to it, or output by the pixel
// shader, no blending).  For a _UNORM format: c.  For a _UNORM_SRGB format: the sRGB encoding of red, green and blue,
// alpha unchanged.  Where the expectation comes from: the transfer function is the sRGB standard's (IEC 61966-2-1:
// 12.92 c below 0.0031308, else 1.055 c^(1/2.4) - 0.055); that Direct3D 11 stores the encoded value when the render
// target view's format is an _SRGB one is its rule for such formats, written here FROM MEMORY of Microsoft's
// documentation, not checked against the page in this unit.  Nothing here comes from what DXMT returned.
static bool g_srgb = false;  // the current swap chain's format is an _SRGB one
static int srgb_encode(int v) {
  const double c = v / 255.0;
  const double e = c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
  return (int)(e * 255.0 + 0.5);
}
static Rgba stored(Rgba c) { return g_srgb ? Rgba{srgb_encode(c.r), srgb_encode(c.g), srgb_encode(c.b), c.a} : c; }
static bool is_srgb(DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; }
static bool is_bgra(DXGI_FORMAT f) { return f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; }
static const struct { const char *name; DXGI_FORMAT format; } k_formats[] = {
  {"R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM}, {"R8G8B8A8_UNORM_SRGB", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
  {"B8G8R8A8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM}, {"B8G8R8A8_UNORM_SRGB", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
};
static const char *format_name(DXGI_FORMAT f) {
  for (auto &k : k_formats) if (k.format == f) return k.name;
  return "?";
}

struct Vertex { float pos[4]; float col[4]; };

struct Gfx {
  ID3D11Device *dev = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  IDXGIFactory *factory = nullptr;
  IDXGIAdapter *adapter = nullptr;
  IDXGISwapChain *sc = nullptr;
  ID3D11Texture2D *backbuffer = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  ID3D11Texture2D *staging = nullptr;
  ID3D11Texture2D *sync_src = nullptr, *sync_dst = nullptr;
  ID3D11Buffer *vb = nullptr;
  ID3D11VertexShader *vs = nullptr;
  ID3D11PixelShader *ps = nullptr;
  ID3D11InputLayout *il = nullptr;
  UINT width = 0, height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  bool probe = true;
  int frame = 0;          // steps the pattern; never reset
  uint64_t presents = 0;  // successful, non-occluded Present calls = drawables expected from the layer
  double refresh_hz = 0;  // of the display the window is on, as DXGI reports it
};

// Every pixel is the clear colour or the triangle colour; the triangle (-0.5,-0.5) (0,0.5) (0.5,-0.5) covers 1/8 of the
// target, apex up.  Rows are top-down, pixels R,G,B,A.
static bool check_pattern(const std::vector<uint8_t> &px, UINT W, UINT H, int f, const char *what) {
  const Rgba clear = stored(clear_of(f)), tri = stored(tri_of(f));
  auto at = [&](UINT x, UINT y) {
    const uint8_t *p = &px[((size_t)y * W + x) * 4];
    return Rgba{p[0], p[1], p[2], p[3]};
  };
  size_t n_tri = 0, n_clear = 0, n_other = 0;
  for (UINT y = 0; y < H; y++)
    for (UINT x = 0; x < W; x++) {
      Rgba p = at(x, y);
      if (near(p, tri, 1)) n_tri++;
      else if (near(p, clear, 1)) n_clear++;
      else n_other++;
    }
  const double want = (double)W * H / 8.0;
  bool ok = n_other == 0 && n_tri > want * 0.98 && n_tri < want * 1.02;
  // orientation: (0.3W, 0.7H) is inside only when the apex is up; (0.3W, 0.3H) only when it is down
  const struct { double x, y; bool inside; } points[] = {
    {0.5, 0.5, true}, {0.3, 0.7, true}, {0.7, 0.7, true}, {0.5, 0.3, true},
    {0.3, 0.3, false}, {0.7, 0.3, false}, {0.5, 0.2, false}, {0.5, 0.8, false},
    {0.0, 0.0, false}, {0.999, 0.0, false}, {0.0, 0.999, false}, {0.999, 0.999, false},
  };
  for (auto &pt : points) {
    UINT x = (UINT)(pt.x * W), y = (UINT)(pt.y * H);
    if (!near(at(x, y), pt.inside ? tri : clear, 1)) ok = false;
  }
  g_checks++;
  if (!ok) {
    g_fail++;
    Rgba c = at(W / 2, H / 2), o = at(0, 0);
    printf("  FAIL %s frame %d %ux%u: triangle pixels %zu (want %.0f +-2%%), clear %zu, other %zu; centre (%d,%d,%d,%d) want "
           "(%d,%d,%d,%d); corner (%d,%d,%d,%d) want (%d,%d,%d,%d)\n",
           what, f, W, H, n_tri, want, n_clear, n_other, c.r, c.g, c.b, c.a, tri.r, tri.g, tri.b, tri.a, o.r, o.g, o.b, o.a,
           clear.r, clear.g, clear.b, clear.a);
  }
  return ok;
}

static bool create_size_dependent(Gfx &g) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  HR(g.sc->GetDesc(&sd));
  g.width = sd.BufferDesc.Width;
  g.height = sd.BufferDesc.Height;
  g.format = sd.BufferDesc.Format;
  g_srgb = is_srgb(g.format);
  HR(g.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&g.backbuffer));
  if (!g.backbuffer) return false;
  D3D11_TEXTURE2D_DESC td = {};
  g.backbuffer->GetDesc(&td);
  CHECK(td.Width == g.width && td.Height == g.height && td.Format == g.format && (td.BindFlags & D3D11_BIND_RENDER_TARGET),
        "GetBuffer(0): %ux%u format %u bind 0x%x, swap chain says %ux%u format %u", td.Width, td.Height, (unsigned)td.Format,
        td.BindFlags, g.width, g.height, (unsigned)g.format);
  HR(g.dev->CreateRenderTargetView(g.backbuffer, nullptr, &g.rtv));
  td.Usage = D3D11_USAGE_STAGING;
  td.BindFlags = 0;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  td.MiscFlags = 0;
  HR(g.dev->CreateTexture2D(&td, nullptr, &g.staging));
  return g.rtv && g.staging;
}

// A swap chain's buffers must have no outstanding references when ResizeBuffers is called or the chain is released.
static void release_size_dependent(Gfx &g) {
  g.ctx->OMSetRenderTargets(0, nullptr, nullptr);
  if (g.rtv) { g.rtv->Release(); g.rtv = nullptr; }
  if (g.staging) { g.staging->Release(); g.staging = nullptr; }
  if (g.backbuffer) { g.backbuffer->Release(); g.backbuffer = nullptr; }
}

// Returns when the GPU has finished everything submitted so far: a copy into a staging texture and a read mapping of it.
static void gpu_sync(Gfx &g) {
  g.ctx->CopyResource(g.sync_dst, g.sync_src);
  D3D11_MAPPED_SUBRESOURCE m = {};
  HR(g.ctx->Map(g.sync_dst, 0, D3D11_MAP_READ, 0, &m));
  g.ctx->Unmap(g.sync_dst, 0);
}

struct FrameTimes {
  int f;
  double t_start, t_present_called, t_present_returned;
  uint64_t drawable;  // index in the probe, 0 if unknown
};

static void draw_pattern(Gfx &g, int f) {
  const Rgba clear = clear_of(f), tri = tri_of(f);
  const float cf[4] = {clear.r / 255.f, clear.g / 255.f, clear.b / 255.f, 1.f};
  const float tf[4] = {tri.r / 255.f, tri.g / 255.f, tri.b / 255.f, 1.f};
  const Vertex vertices[3] = {  // clockwise on screen
    {{-0.5f, -0.5f, 0.f, 1.f}, {tf[0], tf[1], tf[2], tf[3]}},
    {{0.0f, 0.5f, 0.f, 1.f}, {tf[0], tf[1], tf[2], tf[3]}},
    {{0.5f, -0.5f, 0.f, 1.f}, {tf[0], tf[1], tf[2], tf[3]}},
  };
  const UINT stride = sizeof(Vertex), offset = 0;
  D3D11_VIEWPORT vp = {0.f, 0.f, (float)g.width, (float)g.height, 0.f, 1.f};
  g.ctx->UpdateSubresource(g.vb, 0, nullptr, vertices, sizeof(vertices), 0);
  g.ctx->IASetInputLayout(g.il);
  g.ctx->IASetVertexBuffers(0, 1, &g.vb, &stride, &offset);
  g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g.ctx->VSSetShader(g.vs, nullptr, 0);
  g.ctx->PSSetShader(g.ps, nullptr, 0);
  g.ctx->RSSetViewports(1, &vp);
  g.ctx->OMSetRenderTargets(1, &g.rtv, nullptr);
  g.ctx->ClearRenderTargetView(g.rtv, cf);
  g.ctx->Draw(3, 0);
}

// The drawable DXMT took for the most recent Present holds pattern f at the swap chain's size.  Call after gpu_sync.
static bool check_drawable(Gfx &g, uint64_t index, int f) {
  WlDrawable d = {};
  if (!wl_probe_get(index, &d)) { CHECK(false, "frame %d: no drawable number %llu", f, (unsigned long long)index); return false; }
  CHECK(d.got_drawable, "frame %d: nextDrawable returned nil", f);
  CHECK(d.width == g.width && d.height == g.height, "frame %d: drawable is %ux%u, swap chain is %ux%u", f, d.width, d.height, g.width, g.height);
  if (!d.got_drawable || d.width != g.width || d.height != g.height) return false;
  std::vector<uint8_t> px((size_t)g.width * g.height * 4);
  int read = wl_probe_read_rgba(index, px.data(), g.width, g.height);
  CHECK(read, "frame %d: could not copy the drawable back (pixel format %u)", f, d.pixel_format);
  return read && check_pattern(px, g.width, g.height, f, "drawable after Present");
}

// One frame checked in full: draw, read back and check, Present, sync, check the drawable.
static bool render_frame(Gfx &g, UINT sync_interval, FrameTimes *times) {
  Pool pool;
  const int f = g.frame++;
  times->f = f;
  times->drawable = 0;
  times->t_start = wl_now();
  draw_pattern(g, f);

  // the back buffer, before Present
  std::vector<uint8_t> px((size_t)g.width * g.height * 4);
  g.ctx->CopyResource(g.staging, g.backbuffer);
  D3D11_MAPPED_SUBRESOURCE m = {};
  HRESULT hr = g.ctx->Map(g.staging, 0, D3D11_MAP_READ, 0, &m);
  CHECK(hr == S_OK, "Map(staging copy of the back buffer) returned 0x%08x", (unsigned)hr);
  if (hr != S_OK) return false;
  for (UINT y = 0; y < g.height; y++) memcpy(&px[(size_t)y * g.width * 4], (const uint8_t *)m.pData + (size_t)y * m.RowPitch, (size_t)g.width * 4);
  g.ctx->Unmap(g.staging, 0);
  if (is_bgra(g.format))
    for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
  bool ok = check_pattern(px, g.width, g.height, f, "back buffer before Present");

  const uint64_t drawables_before = wl_probe_count();
  times->t_present_called = wl_now();
  hr = g.sc->Present(sync_interval, 0);
  times->t_present_returned = wl_now();
  CHECK(hr == S_OK, "Present(%u, 0) returned 0x%08x", sync_interval, (unsigned)hr);
  if (hr != S_OK) return false;
  g.presents++;
  gpu_sync(g);

  if (g.probe) {
    // exactly one drawable was taken from our layer for this Present, of the swap chain's size, with the same pixels
    const uint64_t drawables = wl_probe_count();
    CHECK(drawables == drawables_before + 1, "frame %d: the layer handed out %llu drawables for one Present", f,
          (unsigned long long)(drawables - drawables_before));
    if (drawables != drawables_before + 1) return false;
    times->drawable = drawables;
    ok = check_drawable(g, drawables, f) && ok;
  }
  return ok;
}

// One frame of a pacing phase: draw and Present, nothing that waits for the GPU.
static bool pace_frame(Gfx &g, UINT sync_interval, FrameTimes *times) {
  Pool pool;
  const int f = g.frame++;
  times->f = f;
  times->drawable = 0;
  times->t_start = wl_now();
  draw_pattern(g, f);
  times->t_present_called = wl_now();
  HRESULT hr = g.sc->Present(sync_interval, 0);
  times->t_present_returned = wl_now();
  CHECK(hr == S_OK, "Present(%u, 0) returned 0x%08x", sync_interval, (unsigned)hr);
  if (hr != S_OK) return false;
  g.presents++;
  return true;
}

// ---- pacing ---------------------------------------------------------------------------------------------------------------

struct Pacing {
  int frames = 0, shown = 0, never_shown = 0, no_report = 0;
  double loop_fps = 0;                                                    // from the times Present was called, steady state
  double shown_fps = 0, mean_ms = 0, min_ms = 0, max_ms = 0, sd_ms = 0;   // from presentedTime, steady state
  double next_wait_mean_ms = 0, next_wait_max_ms = 0, present_call_mean_ms = 0, present_call_max_ms = 0;
  double latency_mean_ms = 0;  // Present called -> shown
};

static Pacing measure(const std::vector<FrameTimes> &frames, Host *host, bool pump) {
  Pacing p;
  p.frames = (int)frames.size();
  if (frames.size() < 4) return p;
  // presented handlers arrive a little after the fact
  for (int waited = 0; waited < 400; waited += 10) {
    WlDrawable last = {};
    if (!frames.back().drawable || (wl_probe_get(frames.back().drawable, &last) && last.handler_ran)) break;
    if (pump) host_pump(host);
    sleep_ms(10);
  }
  const size_t skip = frames.size() / 6;  // the first frames fill the queue
  p.loop_fps = (double)(frames.size() - 1 - skip) / (frames.back().t_present_called - frames[skip].t_present_called);
  std::vector<double> shown_at;
  double wait_sum = 0, call_sum = 0, latency_sum = 0;
  int latency_n = 0;
  for (size_t i = 0; i < frames.size(); i++) {
    double call = (frames[i].t_present_returned - frames[i].t_present_called) * 1e3;
    call_sum += call;
    p.present_call_max_ms = fmax(p.present_call_max_ms, call);
    WlDrawable d = {};
    if (!frames[i].drawable || !wl_probe_get(frames[i].drawable, &d)) continue;
    double wait = (d.t_returned - d.t_called) * 1e3;
    wait_sum += wait;
    p.next_wait_max_ms = fmax(p.next_wait_max_ms, wait);
    if (!d.handler_ran) { p.no_report++; continue; }
    if (d.t_presented <= 0) { p.never_shown++; continue; }
    p.shown++;
    latency_sum += (d.t_presented - frames[i].t_present_called) * 1e3;
    latency_n++;
    if (i >= skip) shown_at.push_back(d.t_presented);
  }
  p.next_wait_mean_ms = wait_sum / frames.size();
  p.present_call_mean_ms = call_sum / frames.size();
  if (latency_n) p.latency_mean_ms = latency_sum / latency_n;
  if (shown_at.size() >= 3) {
    double sum = 0, sq = 0;
    p.min_ms = 1e9;
    const size_t n = shown_at.size() - 1;
    for (size_t i = 1; i < shown_at.size(); i++) {
      double d = (shown_at[i] - shown_at[i - 1]) * 1e3;
      sum += d; sq += d * d;
      p.min_ms = fmin(p.min_ms, d); p.max_ms = fmax(p.max_ms, d);
    }
    p.mean_ms = sum / n;
    p.sd_ms = sqrt(fmax(0.0, sq / n - p.mean_ms * p.mean_ms));
    p.shown_fps = 1e3 / p.mean_ms;
  }
  return p;
}

static void print_pacing(const char *label, const Pacing &p) {
  printf("  %s: %d frames; Present loop %.1f fps; Present() call mean %.3f max %.2f ms; nextDrawable wait mean %.3f max %.2f ms\n", label, p.frames, p.loop_fps,
         p.present_call_mean_ms, p.present_call_max_ms, p.next_wait_mean_ms, p.next_wait_max_ms);
  printf("  %s: Core Animation: shown %d, never shown %d, no report %d", label, p.shown, p.never_shown, p.no_report);
  if (p.shown >= 3)
    printf("; shown-to-shown mean %.2f ms (%.1f fps) min %.2f max %.2f sd %.2f; Present-to-shown mean %.1f ms", p.mean_ms, p.shown_fps, p.min_ms, p.max_ms, p.sd_ms,
           p.latency_mean_ms);
  printf("\n");
}

// What DXMT asked Metal to do with the drawables of presents first+1 .. first+count: for SyncInterval 0 an immediate
// present, otherwise "not before the previous frame has been up for SyncInterval refresh periods".
static void check_requests(const char *label, uint64_t first, uint64_t count, UINT sync_interval, double refresh_hz) {
  int n[3] = {0, 0, 0}, wrong = 0;
  double lo = 1e9, hi = 0;
  const double want = sync_interval / refresh_hz;
  for (uint64_t i = first + 1; i <= first + count; i++) {
    WlPresent r = {};
    if (!wl_present_get(i, &r)) { wrong++; continue; }
    n[r.kind]++;
    if (r.kind == 1) { lo = fmin(lo, r.argument); hi = fmax(hi, r.argument); }
    if (sync_interval == 0 ? r.kind != 0 : (r.kind != 1 || fabs(r.argument - want) > want * 0.01)) wrong++;
  }
  WlPresent last = {};
  wl_present_get(first + count, &last);
  printf("  %s: DXMT asked Metal (on thread \"%s\"): presentDrawable: x %d, presentDrawable:afterMinimumDuration: x %d", label, last.thread_name, n[0], n[1]);
  if (n[1]) printf(" (%.3f to %.3f ms)", lo * 1e3, hi * 1e3);
  if (n[2]) printf(", presentDrawable:atTime: x %d", n[2]);
  printf("; expected for SyncInterval %u at %.2f Hz: %s", sync_interval, refresh_hz, sync_interval ? "afterMinimumDuration " : "plain presentDrawable:\n");
  if (sync_interval) printf("%.3f ms\n", want * 1e3);
  CHECK(wrong == 0, "%s: %d of %llu present requests are not what SyncInterval %u at %.2f Hz calls for", label, wrong, (unsigned long long)count, sync_interval, refresh_hz);
}

// What Core Animation says about when the frames were shown; only checkable when it showed them.
static bool check_shown(const char *label, const Pacing &p, UINT sync_interval, double refresh_hz) {
  if (p.shown < p.frames / 2 || p.shown < 3) return false;
  const double period = 1e3 / refresh_hz * (sync_interval ? sync_interval : 1);
  if (sync_interval) {
    // not faster than asked; an upper bound loose enough for a busy machine, tight enough to catch a stall
    CHECK(p.mean_ms > period * 0.95, "%s: frames were shown every %.2f ms on average, faster than the %.2f ms asked for", label, p.mean_ms, period);
    CHECK(p.mean_ms < period * 2.2, "%s: frames were shown every %.2f ms on average, the request was %.2f ms", label, p.mean_ms, period);
    CHECK(p.min_ms > period * 0.5, "%s: two frames were shown %.2f ms apart, less than half of the %.2f ms asked for", label, p.min_ms, period);
  }
  return true;
}

static std::vector<FrameTimes> run_frames(Gfx &g, Host *host, int n, UINT sync_interval, bool pump) {
  std::vector<FrameTimes> frames;
  const uint64_t requests_before = wl_present_count();
  for (int i = 0; i < n; i++) {
    if (pump) host_pump(host);
    FrameTimes t = {};
    if (!render_frame(g, sync_interval, &t)) break;  // one report per broken phase is enough
    frames.push_back(t);
  }
  CHECK((int)frames.size() == n, "only %zu of %d frames were rendered and presented correctly", frames.size(), n);
  if (g.probe && (int)frames.size() == n) {
    CHECK(wl_present_count() == requests_before + n, "%llu present requests for %d frames", (unsigned long long)(wl_present_count() - requests_before), n);
  }
  return frames;
}

// A pacing phase: n frames of draw + Present; afterwards the last drawable is checked and the requests are counted.
static std::vector<FrameTimes> run_pacing(Gfx &g, Host *host, const char *label, int n, UINT sync_interval) {
  std::vector<FrameTimes> frames;
  const uint64_t drawables_before = wl_probe_count(), requests_before = wl_present_count();
  for (int i = 0; i < n; i++) {
    host_pump(host);
    FrameTimes t = {};
    if (!pace_frame(g, sync_interval, &t)) break;
    frames.push_back(t);
  }
  gpu_sync(g);
  CHECK((int)frames.size() == n, "%s: only %zu of %d frames were presented", label, frames.size(), n);
  if (g.probe && (int)frames.size() == n) {
    CHECK(wl_probe_count() == drawables_before + n, "%s: the layer handed out %llu drawables for %d presents", label, (unsigned long long)(wl_probe_count() - drawables_before), n);
    CHECK(wl_present_count() == requests_before + n, "%s: %llu present requests for %d presents", label, (unsigned long long)(wl_present_count() - requests_before), n);
    if (wl_probe_count() == drawables_before + n) {
      for (int i = 0; i < n; i++) frames[i].drawable = drawables_before + i + 1;
      check_drawable(g, drawables_before + n, frames.back().f);  // what is on (or headed for) the screen is the last frame drawn
    }
    if (wl_present_count() == requests_before + n) check_requests(label, requests_before, n, sync_interval, g.refresh_hz);
  }
  return frames;
}

// ---- helpers ------------------------------------------------------------------------------------------------------------

static std::string narrow(const WCHAR *s, size_t max) {
  std::string out;
  for (size_t i = 0; i < max && s[i]; i++) out += (s[i] < 0x80) ? (char)s[i] : '?';
  return out;
}

static HRESULT create_swap_chain(Gfx &g, HWND hwnd, UINT w, UINT h, DXGI_FORMAT format, UINT buffers, IDXGISwapChain **out) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferDesc.Width = w;
  sd.BufferDesc.Height = h;
  sd.BufferDesc.Format = format;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = buffers;
  sd.OutputWindow = hwnd;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  return g.factory->CreateSwapChain(g.dev, &sd, out);
}

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  if (argc < 2) {
    fprintf(stderr, "usage: %s <DXMT meson build dir> [--frames N] [--own-layer] [--hidpi] [--no-probe] [--no-pool] [--hidden] [--foreground] [--hold S] [--timeout S] [--format NAME] [--buffers N]\n", argv[0]);
    return 2;
  }
  const std::string build_dir = argv[1];
  int frames_per_phase = 72, hold_seconds = 0, timeout_seconds = 30, max_drawables = 0;
  bool own_layer = false, hidpi = false, probe = true, foreground = false, hidden = false, stale_size = false;
  DXGI_FORMAT sc_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  UINT sc_buffers = 2;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--own-layer")) own_layer = true;
    else if (!strcmp(argv[i], "--hidpi")) hidpi = true;
    else if (!strcmp(argv[i], "--no-probe")) probe = false;
    else if (!strcmp(argv[i], "--no-pool")) g_use_pools = false;
    else if (!strcmp(argv[i], "--foreground")) foreground = true;
    else if (!strcmp(argv[i], "--hidden")) hidden = true;
    else if (!strcmp(argv[i], "--stale-size")) stale_size = true;
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames_per_phase = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold_seconds = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--max-drawables") && i + 1 < argc) max_drawables = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout_seconds = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--buffers") && i + 1 < argc) sc_buffers = (UINT)atoi(argv[++i]);
    else if (!strcmp(argv[i], "--format") && i + 1 < argc) {
      sc_format = DXGI_FORMAT_UNKNOWN;
      for (auto &k : k_formats) if (!strcmp(argv[i + 1], k.name)) sc_format = k.format;
      if (sc_format == DXGI_FORMAT_UNKNOWN) { fprintf(stderr, "unknown format %s\n", argv[i + 1]); return 2; }
      i++;
    }
    else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
  }
  if (frames_per_phase < 24) frames_per_phase = 24;
  if (frames_per_phase > 600) frames_per_phase = 600;
  if (hold_seconds < 0) hold_seconds = 0;
  if (hold_seconds > 20) hold_seconds = 20;
  if (own_layer && !probe) { fprintf(stderr, "--own-layer always uses the probed layer class\n"); return 2; }

  // Never leave a window behind: whatever happens, this process ends by itself, and its window with it.
  timeout_seconds += hold_seconds;
  std::thread([timeout_seconds] {
    sleep(timeout_seconds);
    fprintf(stderr, "window_test: WATCHDOG: still running after %d s, exiting\n", timeout_seconds);
    printf("WINDOW_TEST_FAIL (watchdog timeout)\n");
    fflush(stdout);
    _exit(3);
  }).detach();

  // DXMT must not write into the user's cache directory or the current directory unless the caller chose a place.
  if (!getenv("DXMT_SHADER_CACHE_PATH")) {
    std::string dir = build_dir + "/tf2-window";
    mkdir(dir.c_str(), 0755);
    dir += "/shader-cache";
    mkdir(dir.c_str(), 0755);
    setenv("DXMT_SHADER_CACHE_PATH", dir.c_str(), 1);
  }
  if (!getenv("DXMT_LOG_PATH")) setenv("DXMT_LOG_PATH", "none", 1);

  // ---- SDL: not a foreground app, the window is not activated when shown ----
  if (!foreground) {
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 2; }
  if (!foreground) wl_set_accessory_app();
  printf("SDL %d.%d.%d, video driver %s; layer from %s; %s; probe %s; autorelease pools %s\n", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
         SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetCurrentVideoDriver(),
         own_layer ? "a view of our own" : "SDL_Metal_CreateView", hidpi ? "swap chain in physical pixels" : "swap chain in points",
         probe ? "on" : "off", g_use_pools ? "on" : "OFF");
  if (probe && !wl_present_hook_install()) { fprintf(stderr, "cannot hook the command buffer's presentDrawable methods; run with --no-probe\n"); return 2; }
  Pool main_pool;  // drained between the phases; every frame has its own

  Dxmt dx;
  if (!dxmt_load(build_dir, &dx)) return 2;

  Gfx g;
  g.probe = probe;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL level = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = dx.create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &g.dev, &level, &g.ctx);
  printf("D3D11CreateDevice hr=0x%08x feature_level=0x%04x\n", (unsigned)hr, (unsigned)level);
  if (FAILED(hr)) return 2;
  {
    // the way an engine that only imports D3D11CreateDevice gets its factory: device -> adapter -> parent
    IDXGIDevice *dxgi_device = nullptr;
    HR(g.dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_device));
    if (dxgi_device) {
      HR(dxgi_device->GetAdapter(&g.adapter));
      dxgi_device->Release();
    }
    if (g.adapter) HR(g.adapter->GetParent(__uuidof(IDXGIFactory), (void **)&g.factory));
    if (!g.factory) { printf("no IDXGIFactory\n"); return 2; }
  }

  // ---- outputs, before any window exists ----
  printf("\n== outputs ==\n");
  int sdl_display_count = 0;
  SDL_DisplayID *sdl_displays = SDL_GetDisplays(&sdl_display_count);
  int output_count = 0;
  for (UINT i = 0;; i++) {
    IDXGIOutput *output = nullptr;
    hr = g.adapter->EnumOutputs(i, &output);
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    CHECK(hr == S_OK && output, "EnumOutputs(%u) returned 0x%08x", i, (unsigned)hr);
    if (!output) break;
    output_count++;
    DXGI_OUTPUT_DESC od = {};
    HR(output->GetDesc(&od));
    UINT n = 0;
    HR(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, nullptr));
    std::vector<DXGI_MODE_DESC> modes(n ? n : 1);
    if (n) HR(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, modes.data()));
    modes.resize(n);
    DXGI_MODE_DESC want = {}, current = {};
    want.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    HR(output->FindClosestMatchingMode(&want, &current, g.dev));
    const int dw = od.DesktopCoordinates.right - od.DesktopCoordinates.left, dh = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
    const double current_hz = current.RefreshRate.Denominator ? (double)current.RefreshRate.Numerator / current.RefreshRate.Denominator : 0;
    printf("output %u: \"%s\" desktop (%d,%d)-(%d,%d) attached=%d monitor=%p; %u modes for R8G8B8A8_UNORM, from %ux%u to %ux%u; current %ux%u @ %.3f Hz\n",
           i, narrow(od.DeviceName, 32).c_str(), (int)od.DesktopCoordinates.left, (int)od.DesktopCoordinates.top, (int)od.DesktopCoordinates.right,
           (int)od.DesktopCoordinates.bottom, od.AttachedToDesktop, (void *)od.Monitor, n, n ? modes.front().Width : 0, n ? modes.front().Height : 0,
           n ? modes.back().Width : 0, n ? modes.back().Height : 0, current.Width, current.Height, current_hz);
    CHECK(od.Monitor != nullptr && od.AttachedToDesktop, "output %u has no monitor handle", i);
    CHECK(narrow(od.DeviceName, 32) == "\\\\.\\DISPLAY" + std::to_string(i + 1), "output %u is named \"%s\"", i, narrow(od.DeviceName, 32).c_str());
    CHECK(n > 0, "output %u has no display modes", i);
    bool sorted = true, sane = true, has_current = false;
    for (UINT k = 0; k < n; k++) {
      const DXGI_MODE_DESC &m = modes[k];
      if (!m.Width || !m.Height || !m.RefreshRate.Numerator || !m.RefreshRate.Denominator || m.Format != DXGI_FORMAT_R8G8B8A8_UNORM) sane = false;
      if (k && (m.Width < modes[k - 1].Width || (m.Width == modes[k - 1].Width && m.Height < modes[k - 1].Height))) sorted = false;
      if ((int)m.Width == dw && (int)m.Height == dh) has_current = true;
    }
    CHECK(sane, "output %u: a mode has a zero size or rate or the wrong format", i);
    CHECK(sorted, "output %u: the mode list is not sorted by width, then height", i);
    CHECK(has_current, "output %u: no mode has the desktop's size %dx%d", i, dw, dh);
    CHECK((int)current.Width == dw && (int)current.Height == dh && current_hz > 1, "output %u: closest mode to 'anything' is %ux%u @ %.2f, desktop is %dx%d",
          i, current.Width, current.Height, current_hz, dw, dh);
    // the same display as SDL sees it (SDL's first display is the primary one)
    if (sdl_displays && (int)i < sdl_display_count) {
      SDL_Rect bounds = {};
      SDL_GetDisplayBounds(sdl_displays[i], &bounds);
      const SDL_DisplayMode *sm = SDL_GetCurrentDisplayMode(sdl_displays[i]);
      printf("  SDL display %u: \"%s\" bounds (%d,%d) %dx%d, current mode %dx%d @ %.3f Hz, pixel density %.2f\n", sdl_displays[i],
             SDL_GetDisplayName(sdl_displays[i]), bounds.x, bounds.y, bounds.w, bounds.h, sm ? sm->w : 0, sm ? sm->h : 0, sm ? sm->refresh_rate : 0.f,
             sm ? sm->pixel_density : 0.f);
      if (i == 0) {
        CHECK(bounds.x == od.DesktopCoordinates.left && bounds.y == od.DesktopCoordinates.top && bounds.w == dw && bounds.h == dh,
              "output 0's desktop rectangle differs from SDL's primary display bounds");
        CHECK(sm && fabs(sm->refresh_rate - current_hz) < 0.5, "output 0's refresh rate %.3f differs from SDL's %.3f", current_hz, sm ? sm->refresh_rate : 0.f);
      }
    }
    output->Release();
  }
  printf("%d DXGI outputs, %d SDL displays\n", output_count, sdl_display_count);
  CHECK(output_count >= 1, "no IDXGIOutput can be enumerated");
  SDL_free(sdl_displays);

  // ---- the window and its layer (main thread) ----
  printf("\n== window ==\n");
  Host host;
  host.hidpi = hidpi;
  SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
  if (!own_layer) flags |= SDL_WINDOW_METAL;
  if (hidpi) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  host.window = SDL_CreateWindow("dxmt window_test", 640, 360, flags);
  if (!host.window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 2; }
  host.nswindow = SDL_GetPointerProperty(SDL_GetWindowProperties(host.window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  if (!host.nswindow) { fprintf(stderr, "no NSWindow behind the SDL window\n"); return 2; }
  SDL_SetWindowPosition(host.window, 60, 90);
  if (own_layer) {
    host.layer = wl_ownview_create(host.nswindow, hidpi ? 0.0 : 1.0, &host.own_view);
  } else {
    host.sdl_view = SDL_Metal_CreateView(host.window);
    host.layer = host.sdl_view ? SDL_Metal_GetLayer(host.sdl_view) : nullptr;
    if (host.layer && probe && !wl_probe_install(host.layer)) {
      fprintf(stderr, "cannot probe SDL's layer (it is not a plain CAMetalLayer); run with --no-probe\n");
      return 2;
    }
  }
  if (!host.layer) { fprintf(stderr, "no CAMetalLayer: %s\n", SDL_GetError()); return 2; }
  if (max_drawables == 2 || max_drawables == 3) wl_layer_set_maximum_drawable_count(host.layer, (unsigned)max_drawables);
  void *layer_weak = wl_weak_new(host.layer);
  const long layer_refs_start = wl_retain_count(host.layer);
  if (!hidden) SDL_ShowWindow(host.window);
  for (int i = 0; i < 5; i++) { host_pump(&host); sleep_ms(10); }
  host_refresh(&host);

  host.handle.magic = DXMT_NATIVE_WINDOW_MAGIC;
  host.handle.version = DXMT_NATIVE_WINDOW_VERSION;
  host.handle.metal_layer = host.layer;
  host.handle.user = &host;
  host.handle.get_client_size = host_get_client_size;
  host.handle.get_display_id = host_get_display_id;
  host.handle.is_minimized = host_is_minimized;
  host.handle.is_foreground = nullptr;
  host.handle.request_client_size = host_request_client_size;
  const HWND hwnd = (HWND)&host.handle;

  WlLayerInfo li = {};
  wl_layer_info(host.layer, &li);
  int ww = 0, wh = 0, pw = 0, ph = 0;
  SDL_GetWindowSize(host.window, &ww, &wh);
  SDL_GetWindowSizeInPixels(host.window, &pw, &ph);
  printf("window %dx%d points, %dx%d pixels, on CGDirectDisplayID %u; client size given to DXMT %ux%u\n", ww, wh, pw, ph, host.display_id.load(),
         (unsigned)(host.client_size.load() >> 32), (unsigned)host.client_size.load());
  printf("layer before DXMT: class %s bounds %.0fx%.0f contentsScale %.2f drawableSize %.0fx%.0f pixelFormat %u opaque %d framebufferOnly %d "
         "displaySync %d device %d delegate-is-NSView %d maximumDrawableCount %u colorspace %s\n",
         li.class_name, li.bounds_w, li.bounds_h, li.contents_scale, li.drawable_w, li.drawable_h, li.pixel_format, li.opaque, li.framebuffer_only,
         li.display_sync, li.has_device, li.delegate_is_view, li.maximum_drawable_count, li.colorspace);
  const UINT W0 = (UINT)(host.client_size.load() >> 32), H0 = (UINT)host.client_size.load();
  CHECK(W0 >= 640 && H0 >= 360, "the window's client size is %ux%u", W0, H0);

  // ---- things that are not windows ----
  printf("\n== handles that are not windows ==\n");
  {
    DXMTNativeWindow wrong_magic = host.handle, no_layer = host.handle, wrong_version = host.handle;
    wrong_magic.magic = 0x12345678;
    no_layer.metal_layer = nullptr;
    wrong_version.version = 99;
    const struct { const char *what; HWND hwnd; } cases[] = {
      {"null HWND", nullptr}, {"wrong magic number", (HWND)&wrong_magic}, {"no layer", (HWND)&no_layer}, {"unknown version", (HWND)&wrong_version}};
    for (auto &c : cases) {
      IDXGISwapChain *bad = nullptr;
      hr = create_swap_chain(g, c.hwnd, 640, 360, DXGI_FORMAT_R8G8B8A8_UNORM, 2, &bad);
      printf("CreateSwapChain(%s) hr=0x%08x swap chain %p\n", c.what, (unsigned)hr, (void *)bad);
      CHECK(hr == DXGI_ERROR_INVALID_CALL && !bad, "CreateSwapChain(%s) should fail with DXGI_ERROR_INVALID_CALL", c.what);
      if (bad) bad->Release();
    }
    CHECK(wl_probe_count() == 0, "a refused swap chain took a drawable");
  }

  // ---- shaders, buffers, the swap chain ----
  printf("\n== swap chain ==\n");
  {
    const D3D11_INPUT_ELEMENT_DESC elems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    HR(g.dev->CreateVertexShader(kVsDxbc, sizeof(kVsDxbc), nullptr, &g.vs));
    HR(g.dev->CreatePixelShader(kPsDxbc, sizeof(kPsDxbc), nullptr, &g.ps));
    HR(g.dev->CreateInputLayout(elems, 2, kVsDxbc, sizeof(kVsDxbc), &g.il));
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(Vertex) * 3;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    HR(g.dev->CreateBuffer(&bd, nullptr, &g.vb));
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 4; td.Height = 4; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    HR(g.dev->CreateTexture2D(&td, nullptr, &g.sync_src));
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    HR(g.dev->CreateTexture2D(&td, nullptr, &g.sync_dst));
    if (!g.vs || !g.ps || !g.il || !g.vb || !g.sync_src || !g.sync_dst) return 2;
  }
  double t0 = wl_now();
  hr = create_swap_chain(g, hwnd, W0, H0, sc_format, sc_buffers, &g.sc);
  printf("IDXGIFactory::CreateSwapChain(%ux%u %s, %u buffers, DISCARD, windowed) hr=0x%08x in %.1f ms\n", W0, H0, format_name(sc_format), sc_buffers, (unsigned)hr, (wl_now() - t0) * 1e3);
  if (hr != S_OK || !g.sc) { printf("WINDOW_TEST_FAIL (no swap chain)\n"); return 1; }
  double refresh_hz = 0;
  {
    DXGI_SWAP_CHAIN_DESC sd = {};
    HR(g.sc->GetDesc(&sd));
    CHECK(sd.OutputWindow == hwnd && sd.Windowed && sd.BufferDesc.Width == W0 && sd.BufferDesc.Height == H0, "GetDesc does not describe what was asked for");
    CHECK(sd.BufferDesc.Format == sc_format && sd.BufferCount == sc_buffers, "GetDesc says format %u and %u buffers, asked for %u (%s) and %u",
          (unsigned)sd.BufferDesc.Format, sd.BufferCount, (unsigned)sc_format, format_name(sc_format), sc_buffers);
    BOOL fullscreen = TRUE;
    HR(g.sc->GetFullscreenState(&fullscreen, nullptr));
    CHECK(!fullscreen, "a windowed swap chain says it is fullscreen");
    IDXGIOutput *output = nullptr;
    HR(g.sc->GetContainingOutput(&output));
    if (output) {
      DXGI_OUTPUT_DESC od = {};
      HR(output->GetDesc(&od));
      DXGI_MODE_DESC want = {}, current = {};
      want.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      HR(output->FindClosestMatchingMode(&want, &current, g.dev));
      refresh_hz = current.RefreshRate.Denominator ? (double)current.RefreshRate.Numerator / current.RefreshRate.Denominator : 0;
      CHECK((uintptr_t)od.Monitor == host.display_id.load(), "GetContainingOutput is monitor %p, the window is on display %u", (void *)od.Monitor, host.display_id.load());
      const SDL_DisplayMode *sm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(host.window));
      printf("containing output \"%s\", %.3f Hz by DXGI, %.3f Hz by SDL\n", narrow(od.DeviceName, 32).c_str(), refresh_hz, sm ? sm->refresh_rate : 0.f);
      CHECK(sm && fabs(sm->refresh_rate - refresh_hz) < 0.5, "DXGI and SDL disagree about the window's refresh rate");
      output->Release();
    }
    IUnknown *second = nullptr;
    hr = g.sc->GetBuffer(1, __uuidof(ID3D11Texture2D), (void **)&second);
    printf("GetBuffer(1) hr=0x%08x (DXMT serves buffer 0 only)\n", (unsigned)hr);
    if (second) second->Release();
  }
  if (!create_size_dependent(g) || refresh_hz < 1) { printf("WINDOW_TEST_FAIL (setup)\n"); return 1; }
  g.refresh_hz = refresh_hz;
  const double frame_ms = 1e3 / refresh_hz;
  const long layer_refs_chain = wl_retain_count(host.layer);
  const bool asleep = wl_display_asleep(host.display_id.load());
  const bool visible = wl_window_visible(host.nswindow);
  printf("display %u is %s; the screen is %s; the window is %s\n", host.display_id.load(), asleep ? "ASLEEP" : "awake", wl_screen_locked() ? "LOCKED" : "not locked",
         visible ? "visible" : "NOT visible (hidden, covered, behind the lock screen, or the display is off)");
  main_pool.drain();

  // ---- V: every frame checked in full ----
  const int verify_frames = frames_per_phase / 2;
  printf("\n== V: %d frames, Present(1, 0), each checked before and after Present ==\n", verify_frames);
  const uint64_t v_requests = wl_present_count();
  auto v = run_frames(g, &host, verify_frames, 1, true);
  wl_layer_info(host.layer, &li);
  printf("  layer after the first Present: drawableSize %.0fx%.0f contentsScale %.2f pixelFormat %u opaque %d framebufferOnly %d displaySync %d device %d colorspace %s wantsEDR %d\n",
         li.drawable_w, li.drawable_h, li.contents_scale, li.pixel_format, li.opaque, li.framebuffer_only, li.display_sync, li.has_device, li.colorspace, li.wants_edr);
  CHECK(li.drawable_w == g.width && li.drawable_h == g.height && li.has_device && !li.display_sync, "DXMT did not configure the layer as expected");
  if (probe && !v.empty()) {
    WlDrawable d = {};
    wl_probe_get(v.back().drawable, &d);
    printf("  nextDrawable is called on thread \"%s\" (main thread: %d); %zu frames: back buffer and drawable both correct\n", d.thread_name, d.on_main_thread, v.size());
    if ((int)v.size() == verify_frames) check_requests("V", v_requests, verify_frames, 1, refresh_hz);
  }
  print_pacing("V", measure(v, &host, true));
  main_pool.drain();

  // ---- A, A2, B: pacing ----
  bool shown_checked = false;
  printf("\n== A: %d frames, Present(1, 0), nothing else in the loop; display at %.2f Hz (%.2f ms) ==\n", frames_per_phase, refresh_hz, frame_ms);
  auto a = run_pacing(g, &host, "A", frames_per_phase, 1);
  Pacing pa = measure(a, &host, true);
  print_pacing("A", pa);
  if (probe) shown_checked = check_shown("A", pa, 1, refresh_hz);
  main_pool.drain();

  printf("\n== A2: %d frames, Present(2, 0) ==\n", frames_per_phase / 3);
  auto a2 = run_pacing(g, &host, "A2", frames_per_phase / 3, 2);
  Pacing pa2 = measure(a2, &host, true);
  print_pacing("A2", pa2);
  if (probe) check_shown("A2", pa2, 2, refresh_hz);
  main_pool.drain();

  printf("\n== B: %d frames, Present(0, 0) ==\n", frames_per_phase);
  auto b = run_pacing(g, &host, "B", frames_per_phase, 0);
  Pacing pb = measure(b, &host, true);
  print_pacing("B", pb);
  if (probe && shown_checked && pb.frames >= 4) {
    // Only meaningful together with A on a display that shows frames: without sync the loop must not be the slower one.
    CHECK(pb.loop_fps > pa.loop_fps * 0.9, "B: without sync the loop ran at %.1f fps, with sync at %.1f fps", pb.loop_fps, pa.loop_fps);
  }
  if (probe) {
    if (shown_checked) {
      printf("  ON-SCREEN PACING CHECKED: Core Animation reported the frames of A as shown; their times are consistent with the display\n");
    } else {
      const bool asleep_now = wl_display_asleep(host.display_id.load());
      const bool visible_now = wl_window_visible(host.nswindow);
      printf("  ON-SCREEN PACING NOT CHECKED: Core Animation reported %d of %d frames of A as shown (display %s, screen %s, window %s).\n"
             "  Only DXMT's requests to Metal were checked.  Run again with the display awake, the screen unlocked and the window uncovered.\n",
             pa.shown, pa.frames, asleep_now ? "asleep" : "awake", wl_screen_locked() ? "locked" : "not locked", visible_now ? "visible" : "not visible");
      if (!asleep_now && visible_now)
        printf("  WARNING: the display is awake and the window visible, yet the frames were not reported shown.\n");
    }
  }
  main_pool.drain();

  // ---- L: the same loop with the device's maximum frame latency at 1 ----
  {
    IDXGIDevice1 *device1 = nullptr;
    HR(g.dev->QueryInterface(__uuidof(IDXGIDevice1), (void **)&device1));
    if (device1) {
      UINT latency = 0;
      HR(device1->GetMaximumFrameLatency(&latency));
      printf("\n== L: %d frames, Present(1, 0), IDXGIDevice1::SetMaximumFrameLatency(1) (it was %u) ==\n", frames_per_phase / 2, latency);
      HR(device1->SetMaximumFrameLatency(1));
      auto l = run_pacing(g, &host, "L", frames_per_phase / 2, 1);
      Pacing pl = measure(l, &host, true);
      print_pacing("L", pl);
      if (probe) check_shown("L", pl, 1, refresh_hz);
      HR(device1->SetMaximumFrameLatency(latency));
      device1->Release();
    }
  }
  main_pool.drain();

  // ---- C: what the window can tell DXMT, and what DXMT can ask of it ----
  printf("\n== C: minimized, fullscreen, ResizeTarget ==\n");
  {
    uint64_t drawables = wl_probe_count();
    UINT count_before = 0, count_after = 0;
    HR(g.sc->GetLastPresentCount(&count_before));
    host.minimized = 1;
    hr = g.sc->Present(1, 0);
    host.minimized = 0;
    HR(g.sc->GetLastPresentCount(&count_after));
    gpu_sync(g);
    printf("Present while the host says minimized: hr=0x%08x (DXGI_STATUS_OCCLUDED = 0x087a0001), drawables taken %llu, present count %u -> %u\n", (unsigned)hr,
           (unsigned long long)(wl_probe_count() - drawables), count_before, count_after);
    CHECK(hr == DXGI_STATUS_OCCLUDED, "Present on a minimized window returned 0x%08x", (unsigned)hr);
    if (probe) CHECK(wl_probe_count() == drawables, "Present on a minimized window took a drawable");
    CHECK(count_before == g.presents, "GetLastPresentCount is %u after %llu presents", count_before, (unsigned long long)g.presents);

    hr = g.sc->SetFullscreenState(TRUE, nullptr);
    BOOL fullscreen = TRUE;
    HR(g.sc->GetFullscreenState(&fullscreen, nullptr));
    printf("SetFullscreenState(TRUE) hr=0x%08x (DXGI_ERROR_NOT_CURRENTLY_AVAILABLE = 0x887a0022), fullscreen afterwards: %d\n", (unsigned)hr, fullscreen);
    CHECK(FAILED(hr) && !fullscreen, "fullscreen is not implemented and must fail cleanly");
    HR(g.sc->SetFullscreenState(FALSE, nullptr));

    DXGI_MODE_DESC target = {};
    target.Width = 1024; target.Height = 576; target.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    HR(g.sc->ResizeTarget(&target));
    printf("ResizeTarget(1024x576): the host was asked %d time(s) for %ux%u\n", host.size_requests.load(), (unsigned)(host.requested_size.load() >> 32),
           (unsigned)host.requested_size.load());
    CHECK(host.size_requests == 1 && host.requested_size.load() == (((uint64_t)1024 << 32) | 576), "ResizeTarget did not reach the host");
    FrameTimes t = {};
    CHECK(render_frame(g, 1, &t), "a frame after the failed fullscreen request");
  }

  // ---- D: the window changes size; ResizeBuffers(0, 0) asks the host ----
  printf("\n== D: SDL_SetWindowSize(800, 450), ResizeBuffers(0, 0, 0, UNKNOWN, 0) ==\n");
  {
    const uint64_t before = host.client_size.load();
    SDL_SetWindowSize(host.window, 800, 450);
    for (int waited = 0; waited < 1000 && host.client_size.load() == before; waited += 5) { host_pump(&host); sleep_ms(5); }
    host_pump(&host);
    const UINT nw = (UINT)(host.client_size.load() >> 32), nh = (UINT)host.client_size.load();
    wl_layer_info(host.layer, &li);
    printf("  window is now %ux%u; layer bounds %.0fx%.0f, drawableSize %.0fx%.0f (before ResizeBuffers)\n", nw, nh, li.bounds_w, li.bounds_h, li.drawable_w, li.drawable_h);
    CHECK(host.client_size.load() != before && nw >= 800 && nh >= 450, "the window did not change size");
    const int queries = host.size_queries.load();
    release_size_dependent(g);
    t0 = wl_now();
    HR(g.sc->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));
    printf("  ResizeBuffers took %.2f ms and asked the host for the size %d time(s)\n", (wl_now() - t0) * 1e3, host.size_queries.load() - queries);
    CHECK(host.size_queries.load() > queries, "ResizeBuffers(0, 0) did not ask the host for the client size");
    if (!create_size_dependent(g)) { printf("WINDOW_TEST_FAIL (after resize)\n"); return 1; }
    CHECK(g.width == nw && g.height == nh, "after ResizeBuffers(0, 0) the back buffer is %ux%u, the window is %ux%u", g.width, g.height, nw, nh);
    wl_layer_info(host.layer, &li);
    printf("  back buffer %ux%u; layer drawableSize %.0fx%.0f\n", g.width, g.height, li.drawable_w, li.drawable_h);
    auto d = run_frames(g, &host, frames_per_phase / 3, 1, true);
    printf("  %zu frames at %ux%u: back buffer and drawable both correct\n", d.size(), g.width, g.height);
  }
  main_pool.drain();

  // ---- E: a swap chain that is not the window's size ----
  printf("\n== E: ResizeBuffers(0, 320, 180, UNKNOWN, 0) in a larger window ==\n");
  {
    release_size_dependent(g);
    HR(g.sc->ResizeBuffers(0, 320, 180, DXGI_FORMAT_UNKNOWN, 0));
    if (!create_size_dependent(g)) { printf("WINDOW_TEST_FAIL (after resize)\n"); return 1; }
    CHECK(g.width == 320 && g.height == 180, "the back buffer is %ux%u", g.width, g.height);
    auto e = run_frames(g, &host, 8, 1, true);
    wl_layer_info(host.layer, &li);
    printf("  %zu frames; layer bounds %.0fx%.0f, drawableSize %.0fx%.0f (Core Animation scales the drawable to the bounds)\n", e.size(), li.bounds_w, li.bounds_h,
           li.drawable_w, li.drawable_h);
  }
  main_pool.drain();

  if (stale_size && probe) {
    printf("\n== experiment: the window changes size and the application does not call ResizeBuffers ==\n");
    const uint64_t before = host.client_size.load();
    SDL_SetWindowSize(host.window, 900, 500);
    for (int waited = 0; waited < 1000 && host.client_size.load() == before; waited += 5) { host_pump(&host); sleep_ms(5); }
    host_pump(&host);
    wl_layer_info(host.layer, &li);
    const int f = g.frame++;
    draw_pattern(g, f);
    HR(g.sc->Present(1, 0));
    g.presents++;
    gpu_sync(g);
    WlDrawable d = {};
    wl_probe_get(wl_probe_count(), &d);
    printf("  swap chain %ux%u; window now %ux%u; layer bounds %.0fx%.0f, layer drawableSize %.0fx%.0f; the drawable DXMT was given is %ux%u\n", g.width, g.height,
           (unsigned)(host.client_size.load() >> 32), (unsigned)host.client_size.load(), li.bounds_w, li.bounds_h, li.drawable_w, li.drawable_h, d.width, d.height);
    std::vector<uint8_t> px((size_t)d.width * d.height * 4);
    if (d.got_drawable && wl_probe_read_rgba(wl_probe_count(), px.data(), d.width, d.height)) {
      const Rgba clear = stored(clear_of(f)), tri = stored(tri_of(f));
      size_t inside = 0, inside_pattern = 0, outside = 0, outside_pattern = 0;
      for (UINT y = 0; y < d.height; y++)
        for (UINT x = 0; x < d.width; x++) {
          const uint8_t *q = &px[((size_t)y * d.width + x) * 4];
          Rgba c = {q[0], q[1], q[2], q[3]};
          bool pattern = near(c, clear, 1) || near(c, tri, 1);
          if (x < g.width && y < g.height) { inside++; inside_pattern += pattern; }
          else { outside++; outside_pattern += pattern; }
        }
      printf("  pixels of this frame's two colours: %zu of %zu inside the top-left %ux%u, %zu of %zu in the rest of the drawable\n", inside_pattern, inside, g.width, g.height,
             outside_pattern, outside);
    }
  }

  // ---- F: rendering on a second thread ----
  printf("\n== F: ResizeBuffers and Present(1, 0) from a second thread; the main thread sleeps 300 ms, then pumps SDL events ==\n");
  {
    std::atomic<int> state{0};  // 1 = ResizeBuffers returned, 2 = all frames done
    std::atomic<double> resize_returned_at{0};
    double resize_called_at = 0;
    std::vector<FrameTimes> f;
    release_size_dependent(g);
    const int n = frames_per_phase / 3;
    std::thread render([&] {
      Pool thread_pool;
      pthread_setname_np("window_test-render");
      resize_called_at = wl_now();
      HR(g.sc->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));  // sets layer properties: dispatch_sync to the main queue
      resize_returned_at = wl_now();
      state = 1;
      if (create_size_dependent(g)) f = run_frames(g, &host, n, 1, false);
      release_size_dependent(g);
      state = 2;
    });
    const double sleep_from = wl_now();
    sleep_ms(300);  // no run loop, no event pump
    const int state_after_sleep = state.load();
    const double pump_from = wl_now();
    bool finished = false;
    while (wl_now() - pump_from < 8.0) {
      host_pump(&host);
      if (state.load() == 2) { finished = true; break; }
      sleep_ms(1);
    }
    if (!finished) {
      // the render thread is stuck inside DXMT; there is nothing to join
      printf("  FAIL the render thread did not finish (state %d): DXMT's main-thread dispatch was not serviced\nWINDOW_TEST_FAIL\n", state.load());
      fflush(stdout);
      _exit(1);
    }
    render.join();
    const double blocked_ms = (resize_returned_at.load() - resize_called_at) * 1e3;
    printf("  ResizeBuffers on the render thread took %.1f ms; when the main thread woke after %.0f ms it had %s; it returned %.1f ms after the main thread began pumping\n",
           blocked_ms, (pump_from - sleep_from) * 1e3, state_after_sleep ? "already returned" : "not returned", (resize_returned_at.load() - pump_from) * 1e3);
    CHECK(f.size() == (size_t)n, "the render thread presented %zu of %d frames", f.size(), n);
    printf("  %zu frames from the render thread: back buffer and drawable both correct\n", f.size());
    if (!create_size_dependent(g)) { printf("WINDOW_TEST_FAIL (after the threaded phase)\n"); return 1; }
    printf("  main-thread dispatch observed: %s\n", state_after_sleep ? "no (ResizeBuffers did not wait for the main thread)" : "yes (ResizeBuffers waited until the main thread pumped SDL events)");
  }
  main_pool.drain();

  if (hold_seconds > 0) {
    printf("\n== holding for %d s (for a human to look at) ==\n", hold_seconds);
    const double until = wl_now() + hold_seconds;
    FrameTimes t = {};
    while (wl_now() < until && !g_quit_requested) {
      host_pump(&host);
      if (!render_frame(g, 1, &t)) break;
    }
  }

  const long layer_refs_used = wl_retain_count(host.layer);
  char threads[2048];
  wl_thread_names(threads, sizeof(threads));
  printf("\n== threads in the process while the swap chain exists ==\n%s", threads);

  // ---- G: release, create again, release ----
  printf("\n== G: release the swap chain, create a second one (B8G8R8A8_UNORM, size 0x0), release it ==\n");
  {
    UINT last = 0;
    HR(g.sc->GetLastPresentCount(&last));
    CHECK(last == g.presents, "GetLastPresentCount is %u after %llu presents", last, (unsigned long long)g.presents);
    if (probe) CHECK(wl_probe_count() == g.presents, "the layer handed out %llu drawables for %llu presents", (unsigned long long)wl_probe_count(), (unsigned long long)g.presents);
    release_size_dependent(g);
    ULONG refs = g.sc->Release();
    g.sc = nullptr;
    printf("first swap chain: %llu presents, Release() returned %u\n", (unsigned long long)g.presents, (unsigned)refs);
    CHECK(refs == 0, "the swap chain still has %u references", (unsigned)refs);

    hr = create_swap_chain(g, hwnd, 0, 0, DXGI_FORMAT_B8G8R8A8_UNORM, 1, &g.sc);
    printf("CreateSwapChain(0x0 B8G8R8A8_UNORM, 1 buffer) hr=0x%08x\n", (unsigned)hr);
    CHECK(hr == S_OK && g.sc, "the second swap chain was not created");
    if (g.sc && create_size_dependent(g)) {
      const UINT nw = (UINT)(host.client_size.load() >> 32), nh = (UINT)host.client_size.load();
      CHECK(g.width == nw && g.height == nh && g.format == DXGI_FORMAT_B8G8R8A8_UNORM, "the second swap chain is %ux%u format %u, the window is %ux%u", g.width, g.height,
            (unsigned)g.format, nw, nh);
      auto s = run_frames(g, &host, 6, 1, true);
      printf("second swap chain %ux%u: %zu frames, back buffer and drawable both correct\n", g.width, g.height, s.size());
      release_size_dependent(g);
      refs = g.sc->Release();
      g.sc = nullptr;
      CHECK(refs == 0, "the second swap chain still has %u references", (unsigned)refs);
    }
  }

  // ---- teardown ----
  printf("\n== teardown ==\n");
  g.ctx->ClearState();
  g.ctx->Flush();
  ULONG r_vs = g.vs->Release(), r_ps = g.ps->Release(), r_il = g.il->Release(), r_vb = g.vb->Release();
  ULONG r_s1 = g.sync_src->Release(), r_s2 = g.sync_dst->Release();
  ULONG r_factory = g.factory->Release(), r_adapter = g.adapter->Release();
  ULONG r_ctx = g.ctx->Release(), r_dev = g.dev->Release();
  printf("Release(): vs %u ps %u layout %u vb %u sync textures %u %u factory %u adapter %u context %u device %u\n", (unsigned)r_vs, (unsigned)r_ps, (unsigned)r_il,
         (unsigned)r_vb, (unsigned)r_s1, (unsigned)r_s2, (unsigned)r_factory, (unsigned)r_adapter, (unsigned)r_ctx, (unsigned)r_dev);
  CHECK(r_dev == 0, "the device still has %u references", (unsigned)r_dev);
  wl_probe_forget();
  main_pool.drain();
  const long layer_refs_end = wl_retain_count(host.layer);
  printf("CAMetalLayer retain count: %ld before the first swap chain, %ld with it, %ld after all presents, %ld after DXMT released everything\n", layer_refs_start,
         layer_refs_chain, layer_refs_used, layer_refs_end);
  if (host.own_view) wl_ownview_destroy(host.own_view);
  if (host.sdl_view) SDL_Metal_DestroyView(host.sdl_view);
  SDL_DestroyWindow(host.window);
  int waited = 0;
  for (; waited < 2000 && wl_weak_alive(layer_weak); waited += 10) { SDL_PumpEvents(); main_pool.drain(); sleep_ms(10); }
  printf("the layer was %s after the window was destroyed (%d ms)\n", wl_weak_alive(layer_weak) ? "NOT deallocated" : "deallocated", waited);
  if (g_use_pools) CHECK(!wl_weak_alive(layer_weak), "something still holds the CAMetalLayer");
  else {
    printf("  (--no-pool: not counted as a failure; this run shows what the missing autorelease pools keep alive;\n"
           "   the main thread's pending autoreleases are listed on stderr)\n");
    fflush(stdout);
    wl_pool_print();
  }
  SDL_Quit();

  printf("\n%d checks, %d failed\n", g_checks, g_fail);
  if (g_fail) { printf("WINDOW_TEST_FAIL\n"); return 1; }
  printf("WINDOW_TEST_OK\n");
  return 0;
}
