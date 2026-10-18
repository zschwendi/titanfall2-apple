// swapchain.cpp -- the swap chain of the DXMT back end (unit dxback-swapchain): the five entries CreateSwapChain,
// SwapChainGetBuffer, ResizeBuffers, Present and SetMaximumFrameLatency, and the shot of a Present (RT_D3D_SHOT).
//
// The window: a swap chain is made on the runtime's own window (OutputWindow is the runtime's handle).  Its layer
// comes from rt_window_host_layer (RT_HOST_WINDOW=appkit or appkit-hidden); DXMT draws on a DXMTNativeWindow made
// here, which stays at its address until the swap chain is released (third_party/dxmt/.../dxmt_native_window.h).
#include "dxback_priv.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "dxmt_native_window.h"

extern "C" {
void *rt_window_host_layer(HWND h);                 // rt/window.h: the host's layer for a top-level window, or NULL
bool  rt_host_window_wanted(void);                  // rt/window.h: RT_HOST_WINDOW is appkit or appkit-hidden
// rt/host.h (its other header would bring the runtime's Windows types in): the same structure, field for field.
struct DxbHostWindowDesc { uint32_t width_px, height_px; int32_t origin_x, origin_y; const char *title; bool hidden;
    bool fullscreen; /* a borderless window covering the main screen; the client area is shown scaled
                        to the largest size of its own shape that fits */ };
void *rt_host_window_open(const DxbHostWindowDesc *d);
void *objc_autoreleasePoolPush(void);              // the Objective-C runtime
void objc_autoreleasePoolPop(void *pool);
}

// The native window of one swap chain: the struct DXMT reads, and the size it answers with.
struct DxbNativeRec {
    DXMTNativeWindow win;
    uint32_t width, height;
    void *device;                                  // the device the swap chain was made on (not owned)
};

static std::mutex g_mu;
static std::map<void *, DxbNativeRec *> g_swaps;   // swap chain (DXMT's object) -> its native window

static void native_client_size(void *user, uint32_t *w, uint32_t *h) {
    const DxbNativeRec *r = static_cast<const DxbNativeRec *>(user);
    *w = r->width;
    *h = r->height;
}

// The factory the device was made by: device -> IDXGIDevice -> adapter -> parent, as tools/dxmt/window_test.cpp:
// the caller releases it.  NULL when any step fails.
static IDXGIFactory *factory_of(ID3D11Device *dev) {
    IDXGIDevice *dxgi = nullptr;
    IDXGIAdapter *adapter = nullptr;
    IDXGIFactory *factory = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi)) && dxgi) {
        if (SUCCEEDED(dxgi->GetAdapter(&adapter)) && adapter)
            adapter->GetParent(__uuidof(IDXGIFactory), (void **)&factory);
        dxgi->Release();
    }
    if (adapter) adapter->Release();
    return factory;
}

static HRESULT b_CreateSwapChain(void *device, const DXGI_SWAP_CHAIN_DESC *desc, void **swapchain) {
    void *layer = rt_window_host_layer((HWND)desc->OutputWindow);
    if (!layer && rt_host_window_wanted()) {
        // The handle is not a window of the runtime: that is the replay player, whose scripts carry the recorded
        // handle as a number (the planner's decision, 12 October).  The swap chain then gets the process's one host
        // window, opened with the swap chain's own size.  In a run of the game the handle is always a real window.
        const char *e = getenv("RT_HOST_WINDOW");
        DxbHostWindowDesc d = { desc->BufferDesc.Width, desc->BufferDesc.Height, 0, 0, "d3d-replay", e && !strcmp(e, "appkit-hidden"), false };
        layer = rt_host_window_open(&d);
    }
    if (!layer) rt_fatal("dxback: a swap chain needs a host window: set RT_HOST_WINDOW=appkit or appkit-hidden");

    DxbNativeRec *rec = new DxbNativeRec{};
    rec->win.magic = DXMT_NATIVE_WINDOW_MAGIC;
    rec->win.version = DXMT_NATIVE_WINDOW_VERSION;
    rec->win.metal_layer = layer;
    rec->win.user = rec;
    rec->win.get_client_size = native_client_size;
    rec->width = desc->BufferDesc.Width;
    rec->height = desc->BufferDesc.Height;
    rec->device = device;

    DXGI_SWAP_CHAIN_DESC d = *desc;
    d.OutputWindow = (HWND)&rec->win;
    d.Windowed = TRUE;

    IDXGIFactory *factory = factory_of(static_cast<ID3D11Device *>(device));
    if (!factory) {
        delete rec;
        rt_fatal("dxback: CreateSwapChain: the device has no IDXGIFactory");
    }
    IDXGISwapChain *sc = nullptr;
    HRESULT hr = factory->CreateSwapChain(static_cast<IUnknown *>(device), &d, &sc);
    factory->Release();
    if (FAILED(hr) || !sc) {
        delete rec;
        if (sc) sc->Release();
        *swapchain = nullptr;
        return hr;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_swaps[sc] = rec;
    }
    *swapchain = static_cast<void *>(sc);
    return hr;
}

// Release of a swap chain, after DXMT's Release (dxback.cpp): its native window goes with it.
void dxback_swapchain_released(void *swapchain) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_swaps.find(swapchain);
    if (it == g_swaps.end()) return;
    delete it->second;
    g_swaps.erase(it);
}

static HRESULT b_SwapChainGetBuffer(void *swapchain, UINT buffer, void **texture) {
    ID3D11Texture2D *tex = nullptr;
    HRESULT hr = static_cast<IDXGISwapChain *>(swapchain)->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&tex);
    *texture = tex ? static_cast<void *>(tex) : nullptr;
    (void)buffer;   // the front serves buffer 0 only (rt/d3dback.h)
    return hr;
}

static HRESULT b_ResizeBuffers(void *swapchain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    HRESULT hr = static_cast<IDXGISwapChain *>(swapchain)->ResizeBuffers(count, width, height, format, flags);
    if (SUCCEEDED(hr) && width && height) {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_swaps.find(swapchain);
        if (it != g_swaps.end()) { it->second->width = width; it->second->height = height; }
    }
    return hr;
}

// ---- the shots: RT_D3D_SHOT=<n>,<n>,... counts the Present calls of the process from 0 ----
static std::set<unsigned long> shot_numbers() {
    static std::set<unsigned long> set;
    static bool done = false;
    if (done) return set;
    done = true;
    const char *list = getenv("RT_D3D_SHOT");
    if (!list || !*list) return set;
    const char *p = list;
    while (*p) {
        char *end = nullptr;
        unsigned long n = strtoul(p, &end, 10);
        if (end == p || (*end && *end != ',')) rt_fatal("dxback: RT_D3D_SHOT is a list of numbers separated by commas: %s", list);
        set.insert(n);
        p = *end ? end + 1 : end;
    }
    return set;
}

static const char *shot_dir(void) {
    const char *d = getenv("RT_D3D_SHOT_DIR");
    if (!d || !*d) d = "build/shots";
    if (mkdir(d, 0755) != 0 && errno != EEXIST) rt_fatal("dxback: cannot make the directory %s", d);
    return d;
}

static void write_shot(void *device, IDXGISwapChain *sc, unsigned long n) {
    ID3D11Device *dev = static_cast<ID3D11Device *>(device);
    ID3D11DeviceContext *ctx = dxback_immediate_context(device);
    if (!ctx) rt_fatal("dxback: shot of Present %lu: the device's immediate context is not known", n);

    ID3D11Texture2D *back = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back);
    if (FAILED(hr) || !back) rt_fatal("dxback: shot of Present %lu: GetBuffer(0) returned 0x%08x", n, (unsigned)hr);
    D3D11_TEXTURE2D_DESC td = {};
    back->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_R8G8B8A8_UNORM && td.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        td.Format != DXGI_FORMAT_B8G8R8A8_UNORM && td.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        rt_fatal("dxback: shot of Present %lu: format %u is not one the shot writes (R8G8B8A8 or B8G8R8A8, UNORM or SRGB)", n, (unsigned)td.Format);
    const bool bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D *staging = nullptr;
    hr = dev->CreateTexture2D(&sd, nullptr, &staging);
    if (FAILED(hr) || !staging) rt_fatal("dxback: shot of Present %lu: staging texture 0x%08x", n, (unsigned)hr);
    ctx->CopyResource(staging, back);
    D3D11_MAPPED_SUBRESOURCE m = {};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) rt_fatal("dxback: shot of Present %lu: Map returned 0x%08x", n, (unsigned)hr);

    const uint32_t w = td.Width, h = td.Height;
    std::string file = std::string(shot_dir()) + "/present-" + std::to_string(n) + ".rtshot";
    FILE *f = fopen(file.c_str(), "wb");
    if (!f) rt_fatal("dxback: cannot write %s: %s", file.c_str(), strerror(errno));
    const uint8_t magic[8] = {'R', 'T', 'S', 'H', 'O', 'T', '1', 0};
    uint8_t dims[8] = {(uint8_t)w, (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
                       (uint8_t)h, (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24)};
    fwrite(magic, 1, 8, f);
    fwrite(dims, 1, 8, f);
    std::vector<uint8_t> row((size_t)w * 4);
    for (uint32_t y = 0; y < h; y++) {
        memcpy(row.data(), (const uint8_t *)m.pData + (size_t)y * m.RowPitch, row.size());
        if (bgra)
            for (size_t i = 0; i < row.size(); i += 4) { uint8_t t = row[i]; row[i] = row[i + 2]; row[i + 2] = t; }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    ctx->Unmap(staging, 0);
    staging->Release();
    back->Release();
    printf("dxback: shot of Present %lu: %s, %u by %u, format %u\n", n, file.c_str(), w, h, (unsigned)td.Format);
    fflush(stdout);
}

static unsigned long g_presents = 0;   // the Present calls of the process (rule 8: one thread in the context at a time)

static HRESULT b_Present(void *swapchain, UINT sync_interval, UINT flags) {
    const unsigned long n = g_presents++;
    IDXGISwapChain *sc = static_cast<IDXGISwapChain *>(swapchain);
    if (shot_numbers().count(n)) {
        void *dev = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            auto it = g_swaps.find(swapchain);
            if (it != g_swaps.end()) dev = it->second->device;
        }
        if (!dev) rt_fatal("dxback: shot of Present %lu: the swap chain has no device on record", n);
        write_shot(dev, sc, n);
    }
    // RT_D3D_PRESENT=immediate (the ledger's switch, runtime/nulldev/ledger.c): the game's sync interval is
    // not handed on, so a run is not held to the display's rate.  For measurements; a person's run leaves it unset.
    static const bool immediate = [] { const char *v = getenv("RT_D3D_PRESENT"); return v && !strcmp(v, "immediate"); }();
    if (immediate) sync_interval = 0;
    void *pool = objc_autoreleasePoolPush();
    HRESULT hr = sc->Present(sync_interval, flags);
    objc_autoreleasePoolPop(pool);
    return hr;
}

static HRESULT b_SetMaximumFrameLatency(void *device, UINT latency) {
    IDXGIDevice1 *d1 = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->QueryInterface(__uuidof(IDXGIDevice1), (void **)&d1);
    if (FAILED(hr) || !d1) rt_fatal("dxback: SetMaximumFrameLatency: the device has no IDXGIDevice1 (0x%08x)", (unsigned)hr);
    hr = d1->SetMaximumFrameLatency(latency);
    d1->Release();
    return hr;
}

void dxback_fill_swapchain(RtD3dBack &t) {
    t.CreateSwapChain = b_CreateSwapChain;
    t.SwapChainGetBuffer = b_SwapChainGetBuffer;
    t.ResizeBuffers = b_ResizeBuffers;
    t.Present = b_Present;
    t.SetMaximumFrameLatency = b_SetMaximumFrameLatency;
}
