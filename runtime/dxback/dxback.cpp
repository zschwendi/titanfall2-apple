// dxback.cpp -- the first real back end behind the Direct3D device (unit dxback-skeleton; rt/d3dback.h has the
// seam's rules, docs/scout/dxback-shape.md the map, docs/handoff/pic-fork.md "For the back end" the hand-off).
//
// It loads DXMT (a Direct3D 11 on Metal, three libraries built by its own build) and fills the table of the seam.
// Three entries are real: CreateDevice, Release and CreateBuffer.  Every other entry stops the run by name
// ("dxback: <entry> is not built yet"); later units replace one at a time.
//
// Built with DXMT's own Direct3D headers, so RT_D3DBACK_OWN_TYPES is defined before rt/d3dback.h, which then does
// not include rt/win/d3d11.h (two definitions of one struct would clash: docs/scout/dxback-shape.md section 8).
// Nothing else of the runtime is included: what is needed is declared below.
#include "dxback_priv.h"

#include <map>
#include <mutex>

// ---- the table's entry names, from the same list as the enumeration ----
#define DXB_NAME_(entry, iface) #entry,
static const char *const entry_names[] = {RT_D3DBACK_ENTRIES(DXB_NAME_) "?"};
#undef DXB_NAME_
static const char *entry_name(int id) { return entry_names[id]; }

// ---- a stub for every entry that is not built yet: a function of exactly the member's type ----
template <int Id, typename R, typename... A> struct NotBuilt {
    static R fn(A...) { rt_fatal("dxback: %s is not built yet", entry_name(Id)); __builtin_unreachable(); }
};
template <int Id, typename R, typename... A> static auto not_built(R (*)(A...)) -> R (*)(A...) {
    return &NotBuilt<Id, R, A...>::fn;
}

// ---- DXMT's host hooks (tools/dxmt/load_probe.cpp, DXMT's dxmt_native_host.h) ----
static std::atomic<unsigned long> g_lines[8];   // by level 0..7 (DXMT's levels are 0..4)
static std::atomic<unsigned long> g_other;      // levels outside 0..7
static bool g_strict = true;

static int host_log(void *, int level, const char *line) {
    if (level >= 0 && level < 8) g_lines[level]++; else g_other++;
    if (level == DXMT_HOST_LOG_ERROR && g_strict)
        rt_fatal("dxback: strict: DXMT error line: %s", line);   // no Direct3D or DXGI call in here (dxmt_native_host.h l.28-29)
    return 0;                                                   // 0: DXMT writes the line as it always did
}

static void host_abort(void *, const char *message) {
    if (g_strict) rt_fatal("dxback: strict: DXMT aborts: %s", message);
    // not strict: returns, and DXMT calls abort() itself
}

// The counts, printed by the player just before it leaves (runtime/tools/d3dreplay/main.c calls it; it leaves through
// _exit, so an atexit hook would never run).  Printed only once the table was made in this process.
static bool g_built = false;
extern "C" void rt_dxback_report(void) {
    if (!g_built) return;
    fprintf(stderr, "dxback: back end dxmt: DXMT log lines by level: trace=%lu debug=%lu info=%lu warn=%lu error=%lu other=%lu\n",
            g_lines[0].load(), g_lines[1].load(), g_lines[2].load(), g_lines[3].load(), g_lines[4].load(), g_other.load());
}

// The immediate context of each device, from its creation until the device is released (the shot copies on it).
static std::mutex g_ctx_mu;
static std::map<void *, ID3D11DeviceContext *> g_device_ctx;

// ---- the three real entries ----
static const char *g_dir;
static PFN_D3D11_CREATE_DEVICE g_create_device;

static HRESULT b_CreateDevice(void **device, void **context) {
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    D3D_FEATURE_LEVEL level = (D3D_FEATURE_LEVEL)0;
    // as tools/dxmt/window_test.cpp:650: hardware, no adapter, no flags, 11_1 then 11_0
    HRESULT hr = g_create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &dev, &level, &ctx);
    if (FAILED(hr) || !dev || !ctx) {
        if (dev) dev->Release();
        if (ctx) ctx->Release();
        return hr;     // a failure as DXMT gives it; a success without the objects is the front's stop (rule 4)
    }
    {
        std::lock_guard<std::mutex> lock(g_ctx_mu);
        g_device_ctx[static_cast<void *>(dev)] = ctx;   // not owned: the front holds the context's reference
    }
    *device = static_cast<void *>(dev);
    *context = static_cast<void *>(ctx);
    return hr;
}

ID3D11DeviceContext *dxback_immediate_context(void *device) {
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    auto it = g_device_ctx.find(device);
    return it == g_device_ctx.end() ? nullptr : it->second;
}

static void b_Release(RtD3dBackKind kind, void *object) {
    if (kind == RT_BK_DEVICE) {
        std::lock_guard<std::mutex> lock(g_ctx_mu);
        g_device_ctx.erase(object);
    }
    static_cast<IUnknown *>(object)->Release();
    if (kind == RT_BK_SWAPCHAIN) dxback_swapchain_released(object);   // after DXMT's Release: its native window goes too
}

static HRESULT b_CreateBuffer(void *device, const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *initial, void **buffer) {
    ID3D11Buffer *b = nullptr;
    HRESULT hr = static_cast<ID3D11Device *>(device)->CreateBuffer(desc, initial, &b);
    *buffer = b ? static_cast<void *>(b) : nullptr;
    return hr;
}

// ---- the table: once, on the first call ----
static void make_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) rt_fatal("dxback: cannot make the directory %s", path);
}

static void set_env_default(const char *name, const char *value) { setenv(name, value, 0); }

static const RtD3dBack *build_table(void) {
    const char *dir = getenv("RT_DXMT_DIR");
    if (!dir || !*dir)
        rt_fatal("dxback: RT_DXMT_DIR is not set: give the directory of DXMT's winemetal.dylib, dxgi.dylib and d3d11.dylib");
    g_dir = dir;
    g_strict = !(getenv("RT_D3D_STRICT") && !strcmp(getenv("RT_D3D_STRICT"), "0"));

    // the environment before the first dlopen: the log level is read when dxgi.dylib loads (pic-fork.md, 1)
    make_dir("build");
    make_dir("build/dxmt-log");
    make_dir("build/dxmt-cache");
    char log_abs[PATH_MAX], cache_abs[PATH_MAX];
    if (!realpath("build/dxmt-log", log_abs) || !realpath("build/dxmt-cache", cache_abs))
        rt_fatal("dxback: cannot resolve build/dxmt-log or build/dxmt-cache");
    set_env_default("DXMT_LOG_LEVEL", "warn");
    set_env_default("DXMT_LOG_PATH", log_abs);
    set_env_default("DXMT_SHADER_CACHE_PATH", cache_abs);
    // RT_D3D_UPSCALE=<f> (gate3/run.c sets it from GATE3_UPSCALE, already checked there): MetalFX spatial
    // upscaling in DXMT's swapchain.  The game draws at its own size; the drawable is that size times f
    // (docs/scout/metalfx.md).  A DXMT_CONFIG of the caller's is kept and the factor added to it, unless it
    // names a factor itself.
    if (const char *up = getenv("RT_D3D_UPSCALE"); up && *up) {
        set_env_default("DXMT_METALFX_SPATIAL_SWAPCHAIN", "1");
        std::string factor = std::string("d3d11.metalSpatialUpscaleFactor=") + up;
        const char *conf = getenv("DXMT_CONFIG");
        if (!conf || !*conf)
            setenv("DXMT_CONFIG", factor.c_str(), 1);
        else if (!strstr(conf, "metalSpatialUpscaleFactor"))
            setenv("DXMT_CONFIG", (std::string(conf) + ";" + factor).c_str(), 1);
    }

    const char *names[3] = {"winemetal.dylib", "dxgi.dylib", "d3d11.dylib"};
    void *handles[3] = {};
    for (int i = 0; i < 3; i++) {
        std::string path = std::string(dir) + "/" + names[i];
        handles[i] = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handles[i]) rt_fatal("dxback: dlopen of %s failed: %s", path.c_str(), dlerror());
    }
    auto set_log = (void (*)(DXMTHostLogFunction, void *))dlsym(handles[0], "DXMTSetHostLogFunction");
    auto set_abort = (void (*)(DXMTHostAbortFunction, void *))dlsym(handles[0], "DXMTSetHostAbortFunction");
    if (!set_log || !set_abort) rt_fatal("dxback: %s has no DXMTSetHostLogFunction or DXMTSetHostAbortFunction", names[0]);
    set_log(host_log, nullptr);      // before the first DXGI or Direct3D call
    set_abort(host_abort, nullptr);
    g_create_device = (PFN_D3D11_CREATE_DEVICE)dlsym(handles[2], "D3D11CreateDevice");
    if (!g_create_device) rt_fatal("dxback: %s has no D3D11CreateDevice", names[2]);
    g_built = true;

    static RtD3dBack t;
    t = RtD3dBack{};
    t.name = "dxmt";
    t.CreateDevice = b_CreateDevice;
    t.Release = b_Release;
    t.CreateBuffer = b_CreateBuffer;
    // the entries of each group, in their own files (dxback_priv.h lists them)
    dxback_fill_resources(t);
    dxback_fill_shaders(t);
    dxback_fill_states(t);
    dxback_fill_bindbuf(t);
    dxback_fill_bindshader(t);
    dxback_fill_pipeline(t);
    dxback_fill_draw(t);
    dxback_fill_data(t);
    dxback_fill_swapchain(t);
    // a test switch: RT_DXBACK_UNBUILT=<entry name> leaves that built entry to the named stop, as if not built
    if (const char *unbuilt = getenv("RT_DXBACK_UNBUILT")) {
        bool found = false;
#define DXB_UNB_(entry, iface) if (!strcmp(unbuilt, #entry)) { t.entry = nullptr; found = true; }
        RT_D3DBACK_ENTRIES(DXB_UNB_)
#undef DXB_UNB_
        if (!found) rt_fatal("dxback: RT_DXBACK_UNBUILT names no entry: %s", unbuilt);
    }
    // every other entry: a stop by name
#define DXB_NB_(entry, iface) if (!t.entry) t.entry = not_built<RT_BACK_##entry>(t.entry);
    RT_D3DBACK_ENTRIES(DXB_NB_)
#undef DXB_NB_
    return &t;
}

extern "C" const RtD3dBack *rt_dxback_table(void) {
    static const RtD3dBack *table = build_table();   // first call; thread-safe
    return table;
}
