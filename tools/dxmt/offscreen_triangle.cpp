// offscreen_triangle.cpp -- draw a triangle off screen through DXMT's native Direct3D 11 and read the pixels back.
//
// No window.  Nothing is compiled at build time of DXMT or at run time here: the shaders are DXBC byte arrays
// (tools/dxmt/dxbc_blobs.h, hand-assembled by make_dxbc.py) handed to CreateVertexShader/CreatePixelShader; DXMT
// converts them to AIR itself (airconv, statically linked into winemetal.dylib) and Metal compiles the pipeline.
//
// Steps: create device -> 256x256 R8G8B8A8_UNORM render target -> vertex buffer (3 x {float4 pos, float4 colour})
//        -> input layout, VS, PS -> clear to a known colour -> Draw(3) -> CopyResource to a staging texture ->
//        Map -> check pixels inside and outside the triangle.
// Two draws: (A) one solid colour; (B) red/green/blue vertex colours, to see interpolation.  Each is drawn twice
// and timed (the first draw includes the shader conversion and the Metal pipeline compile; the second reuses them).
//
// Usage:  offscreen_triangle <DXMT meson build dir> [--ppm <prefix>] [--salt <N>]
//           --ppm   writes <prefix>_a.ppm and <prefix>_b.ppm
//           --salt  use the pixel shader variant "col + float4(0,0,0,N*1e-9)" (alpha is clamped, so the pixels do not
//                   change) which gives every N a pixel shader that no shader cache has seen: for timing a cold compile
// Exit:   0 if every pixel check passed, 1 if a check failed, 2 on setup failure.
#include "dxmt_load.h"
#include "dxbc_blobs.h"  // hand-assembled DXBC, see make_dxbc.py

#include <chrono>
#include <math.h>
#include <string.h>
#include <vector>

typedef std::chrono::steady_clock Clock;
static double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

static const UINT W = 256, H = 256;

struct Vertex { float pos[4]; float col[4]; };
struct Rgba { int r, g, b, a; };

static int g_fail = 0;
static void check(bool ok, const char *what, int x, int y, Rgba got, Rgba want, int tol) {
  if (!ok) {
    g_fail++;
    printf("  FAIL %-34s at (%3d,%3d): got (%3d,%3d,%3d,%3d) want (%3d,%3d,%3d,%3d) +-%d\n", what, x, y, got.r, got.g, got.b, got.a, want.r, want.g, want.b, want.a, tol);
  }
}
static bool near(Rgba a, Rgba b, int tol) {
  return abs(a.r - b.r) <= tol && abs(a.g - b.g) <= tol && abs(a.b - b.b) <= tol && abs(a.a - b.a) <= tol;
}

struct Ctx {
  ID3D11Device *dev; ID3D11DeviceContext *ctx;
  ID3D11Texture2D *rt, *staging; ID3D11RenderTargetView *rtv;
  ID3D11Buffer *vb;
};

static std::vector<uint8_t> readback(Ctx &c, double *ms_total, Clock::time_point t0) {
  c.ctx->CopyResource(c.staging, c.rt);
  D3D11_MAPPED_SUBRESOURCE m = {};
  HRESULT hr = c.ctx->Map(c.staging, 0, D3D11_MAP_READ, 0, &m);  // blocks until the GPU is done
  std::vector<uint8_t> px(W * H * 4);
  if (FAILED(hr)) { printf("  Map(staging) failed hr=0x%08x\n", (unsigned)hr); g_fail++; return px; }
  for (UINT y = 0; y < H; y++) memcpy(&px[y * W * 4], (const uint8_t *)m.pData + (size_t)y * m.RowPitch, W * 4);
  c.ctx->Unmap(c.staging, 0);
  *ms_total = ms_since(t0);
  return px;
}

static Rgba at(const std::vector<uint8_t> &px, int x, int y) {
  const uint8_t *p = &px[((size_t)y * W + x) * 4];
  return {p[0], p[1], p[2], p[3]};
}

static void draw_once(Ctx &c, const float clear[4], const Vertex *tri) {
  c.ctx->UpdateSubresource(c.vb, 0, nullptr, tri, sizeof(Vertex) * 3, 0);
  c.ctx->ClearRenderTargetView(c.rtv, clear);
  c.ctx->Draw(3, 0);
}

static void ascii_preview(const std::vector<uint8_t> &px, Rgba clear, Rgba tri) {
  for (int y = 0; y < (int)H; y += 8) {
    printf("  ");
    for (int x = 0; x < (int)W; x += 4) {
      Rgba p = at(px, x, y);
      putchar(near(p, clear, 1) ? '.' : near(p, tri, 1) ? '#' : '+');
    }
    putchar('\n');
  }
}

static void write_ppm(const std::string &path, const std::vector<uint8_t> &px) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { printf("  cannot write %s\n", path.c_str()); return; }
  fprintf(f, "P6\n%u %u\n255\n", W, H);
  for (size_t i = 0; i < (size_t)W * H; i++) fwrite(&px[i * 4], 1, 3, f);
  fclose(f);
  printf("  wrote %s\n", path.c_str());
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <DXMT meson build dir> [--ppm <prefix>]\n", argv[0]); return 2; }
  std::string ppm;
  long salt = -1;
  for (int i = 2; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--ppm")) ppm = argv[i + 1];
    if (!strcmp(argv[i], "--salt")) salt = atol(argv[i + 1]);
  }

  Dxmt dx;
  if (!dxmt_load(argv[1], &dx)) return 2;

  Ctx c = {};
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
  HRESULT hr = dx.create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &c.dev, &fl, &c.ctx);
  printf("D3D11CreateDevice hr=0x%08x feature_level=0x%04x\n", (unsigned)hr, (unsigned)fl);
  if (FAILED(hr)) return 2;

  // --- resources ---
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  hr = c.dev->CreateTexture2D(&td, nullptr, &c.rt);
  printf("CreateTexture2D(render target %ux%u R8G8B8A8_UNORM) hr=0x%08x\n", W, H, (unsigned)hr);
  if (FAILED(hr)) return 2;
  hr = c.dev->CreateRenderTargetView(c.rt, nullptr, &c.rtv);
  printf("CreateRenderTargetView hr=0x%08x\n", (unsigned)hr);
  if (FAILED(hr)) return 2;
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  hr = c.dev->CreateTexture2D(&td, nullptr, &c.staging);
  printf("CreateTexture2D(staging, CPU read) hr=0x%08x\n", (unsigned)hr);
  if (FAILED(hr)) return 2;

  const Vertex solid[3] = {  // clockwise on screen (front face for the default rasterizer state)
    {{-0.5f, -0.5f, 0.f, 1.f}, {1.f, 0.8f, 0.f, 1.f}},
    {{ 0.0f,  0.5f, 0.f, 1.f}, {1.f, 0.8f, 0.f, 1.f}},
    {{ 0.5f, -0.5f, 0.f, 1.f}, {1.f, 0.8f, 0.f, 1.f}},
  };
  const Vertex rgb[3] = {
    {{-0.5f, -0.5f, 0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
    {{ 0.0f,  0.5f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}},
    {{ 0.5f, -0.5f, 0.f, 1.f}, {0.f, 0.f, 1.f, 1.f}},
  };
  D3D11_BUFFER_DESC bd = {};
  bd.ByteWidth = sizeof(solid); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA init = {solid, 0, 0};
  hr = c.dev->CreateBuffer(&bd, &init, &c.vb);
  printf("CreateBuffer(vertex buffer, 3 x %zu bytes) hr=0x%08x\n", sizeof(Vertex), (unsigned)hr);
  if (FAILED(hr)) return 2;

  auto t_sh = Clock::now();
  ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps = nullptr; ID3D11InputLayout *il = nullptr;
  HRESULT hv = c.dev->CreateVertexShader(kVsDxbc, sizeof(kVsDxbc), nullptr, &vs);
  std::vector<uint8_t> ps_bytes(kPsDxbc, kPsDxbc + sizeof(kPsDxbc));
  if (salt >= 0) {
    ps_bytes.assign(kPsSaltedDxbc, kPsSaltedDxbc + sizeof(kPsSaltedDxbc));
    float f = 1e-9f * (float)(salt % 100000000 + 1);
    uint32_t bits, placeholder = 0xDEADBEEF;
    memcpy(&bits, &f, 4);
    int n = 0;
    for (size_t i = 0; i + 4 <= ps_bytes.size(); i += 4)
      if (!memcmp(&ps_bytes[i], &placeholder, 4)) { memcpy(&ps_bytes[i], &bits, 4); n++; }
    if (n != 1) { fprintf(stderr, "salt placeholder found %d times\n", n); return 2; }
    printf("pixel shader: salted variant, SALT = %g (alpha += SALT, clamped)\n", f);
  }
  HRESULT hp = c.dev->CreatePixelShader(ps_bytes.data(), ps_bytes.size(), nullptr, &ps);
  const D3D11_INPUT_ELEMENT_DESC elems[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  HRESULT hi = c.dev->CreateInputLayout(elems, 2, kVsDxbc, sizeof(kVsDxbc), &il);
  printf("CreateVertexShader(%zu bytes DXBC vs_5_0) hr=0x%08x; CreatePixelShader(%zu bytes ps_5_0) hr=0x%08x; CreateInputLayout hr=0x%08x\n",
         sizeof(kVsDxbc), (unsigned)hv, ps_bytes.size(), (unsigned)hp, (unsigned)hi);
  double ms_create = ms_since(t_sh);
  if (FAILED(hv) || FAILED(hp) || FAILED(hi)) return 2;

  // --- pipeline state ---
  const UINT stride = sizeof(Vertex), offset = 0;
  D3D11_VIEWPORT vp = {0.f, 0.f, (float)W, (float)H, 0.f, 1.f};
  c.ctx->IASetInputLayout(il);
  c.ctx->IASetVertexBuffers(0, 1, &c.vb, &stride, &offset);
  c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  c.ctx->VSSetShader(vs, nullptr, 0);
  c.ctx->PSSetShader(ps, nullptr, 0);
  c.ctx->RSSetViewports(1, &vp);
  c.ctx->OMSetRenderTargets(1, &c.rtv, nullptr);

  // expected values: clear (0.25, 0.5, 0.75, 1) -> (64, 128, 191, 255) +-1; triangle (1, 0.8, 0, 1) -> (255, 204, 0, 255)
  const float clearf[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  const Rgba clear = {64, 128, 191, 255}, tri = {255, 204, 0, 255};

  // --- A: solid colour ---
  printf("\n== A: solid triangle (1.0, 0.8, 0.0, 1.0) on clear (0.25, 0.5, 0.75, 1.0) ==\n");
  double ms_first = 0, ms_second = 0;
  std::vector<uint8_t> a;
  {
    auto t0 = Clock::now();
    draw_once(c, clearf, solid);
    a = readback(c, &ms_first, t0);
    t0 = Clock::now();
    draw_once(c, clearf, solid);
    a = readback(c, &ms_second, t0);
  }
  ascii_preview(a, clear, tri);
  int n_tri = 0, n_clear = 0, n_other = 0;
  for (UINT y = 0; y < H; y++) for (UINT x = 0; x < W; x++) {
    Rgba p = at(a, x, y);
    if (near(p, tri, 1)) n_tri++; else if (near(p, clear, 1)) n_clear++; else n_other++;
  }
  // The triangle spans NDC x in [-0.5,0.5], y in [-0.5,0.5]: base 128 px, height 128 px, area 8192 px.
  printf("  pixels: triangle colour %d (expected about 8192), clear colour %d, other %d (expected 0)\n", n_tri, n_clear, n_other);
  const int inside[][2] = {{128, 150}, {128, 100}, {128, 80}, {80, 185}, {176, 185}, {128, 190}};
  const int outside[][2] = {{0, 0}, {255, 0}, {0, 255}, {255, 255}, {128, 30}, {128, 250}, {60, 190}, {196, 190}, {90, 100}, {166, 100}};
  for (auto &p : inside) check(near(at(a, p[0], p[1]), tri, 1), "inside triangle", p[0], p[1], at(a, p[0], p[1]), tri, 1);
  for (auto &p : outside) check(near(at(a, p[0], p[1]), clear, 1), "outside triangle (clear colour)", p[0], p[1], at(a, p[0], p[1]), clear, 1);
  if (!(n_tri > 8192 * 0.985 && n_tri < 8192 * 1.015)) { g_fail++; printf("  FAIL triangle pixel count %d not within 1.5%% of 8192\n", n_tri); }
  if (n_other != 0) { g_fail++; printf("  FAIL %d pixels are neither clear nor triangle colour\n", n_other); }
  if (!ppm.empty()) write_ppm(ppm + "_a.ppm", a);
  printf("  A: %s\n", g_fail == 0 ? "PASS" : "FAIL");

  // --- B: interpolated vertex colours ---
  printf("\n== B: red/green/blue vertex colours ==\n");
  int fail_before_b = g_fail;
  {
    auto t0 = Clock::now();
    draw_once(c, clearf, rgb);
    double ms;
    std::vector<uint8_t> b = readback(c, &ms, t0);
    // centroid of the triangle in pixels: (128, 149.33); all three weights 1/3 -> about (85, 85, 85)
    Rgba cen = at(b, 128, 149);
    check(near(cen, {85, 85, 85, 255}, 6), "centroid (equal weights)", 128, 149, cen, {85, 85, 85, 255}, 6);
    Rgba p;
    p = at(b, 72, 186);  check(p.r > 200 && p.g < 60 && p.b < 60, "near red vertex (bottom left)", 72, 186, p, {255, 0, 0, 255}, 55);
    p = at(b, 128, 72);  check(p.g > 200 && p.r < 60 && p.b < 60, "near green vertex (top)", 128, 72, p, {0, 255, 0, 255}, 55);
    p = at(b, 184, 186); check(p.b > 200 && p.r < 60 && p.g < 60, "near blue vertex (bottom right)", 184, 186, p, {0, 0, 255, 255}, 55);
    Rgba o = at(b, 10, 10);
    check(near(o, clear, 1), "outside (clear colour)", 10, 10, o, clear, 1);
    // colours must sum to about 255 at any interior point (barycentric weights): check one off-centre point
    p = at(b, 100, 170);
    int sum = p.r + p.g + p.b;
    check(abs(sum - 255) <= 6, "weights sum to 1 at (100,170)", 100, 170, p, {0, 0, 0, 255}, 6);
    printf("  sample: centroid (%d,%d,%d)  (72,186) (%d,%d,%d)  (128,72) (%d,%d,%d)  (184,186) (%d,%d,%d)\n", cen.r, cen.g, cen.b,
           at(b, 72, 186).r, at(b, 72, 186).g, at(b, 72, 186).b, at(b, 128, 72).r, at(b, 128, 72).g, at(b, 128, 72).b,
           at(b, 184, 186).r, at(b, 184, 186).g, at(b, 184, 186).b);
    if (!ppm.empty()) write_ppm(ppm + "_b.ppm", b);
  }
  printf("  B: %s\n", g_fail == fail_before_b ? "PASS" : "FAIL");

  printf("\ntiming_ms: create_shaders_and_layout=%.2f first_draw_to_readback=%.2f second_draw_to_readback=%.2f\n", ms_create, ms_first, ms_second);
  printf("first draw includes DXBC->AIR conversion and the Metal pipeline compile; the second reuses the pipeline\n");

  vs->Release(); ps->Release(); il->Release(); c.vb->Release(); c.rtv->Release(); c.rt->Release(); c.staging->Release();
  c.ctx->Release(); c.dev->Release();

  if (g_fail) { printf("\nTRIANGLE_FAIL (%d checks failed)\n", g_fail); return 1; }
  printf("\nTRIANGLE_OK\n");
  return 0;
}
