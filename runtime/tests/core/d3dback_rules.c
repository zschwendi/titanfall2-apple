/* The rules of the seam behind the do-nothing device (rt/d3dback.h, "THE RULES OF THE SEAM"):
 * the ones no single method's test can show.  Every expected value is the header's text: which
 * back end is chosen (the environment, or rt_d3dback_use before the first device), what crosses
 * (rule 2), what a fatal report says about the back end (rule 1), which methods never ask it
 * (rule 7), and that a front object's destruction releases its back object once (rule 3).  The
 * entry count, 75, is the header's own statement of its table. */
#include <stdlib.h>
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

static uint64_t reached_total(void) {
    uint64_t n = 0;
    for (unsigned e = 0; e < RT_BACK_N; e++) n += rt_d3dback_record_count((RtD3dBackEntry)e);
    return n;
}

/* A watch runs in the method's thunk before its body: it stands in for a body that hands the
 * call on before its last check. */
static void forward_too_early(Ctx *ctx, const RtComMethod *m, uint64_t nth) {
    (void)m; (void)nth;
    rt_d3dback_reach(ctx, RT_BACK_ClearRenderTargetView);
}

TEST(core, d3dback_rules)
{
    rt_d3dback_use(rt_d3dback_record());
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    const RtD3dBack *b = rt_d3dback();
    FLOAT colour[4] = { 0, 0, 0, 1 };

    T_CASE("the table and its names");
    T_EQ(RT_BACK_N, 75);
    T_TRUE(b == rt_d3dback_record() && !strcmp(b->name, "record"));
    T_TRUE(!strcmp(rt_d3dback_entry_name(RT_BACK_DrawIndexed), "DrawIndexed"));
    T_TRUE(!strcmp(rt_d3dback_entry_method(RT_BACK_Present), "IDXGISwapChain::Present"));
    T_TRUE(!strcmp(rt_d3dback_entry_method(RT_BACK_SetMaximumFrameLatency), "IDXGIDevice1::SetMaximumFrameLatency"));
    T_TRUE(rt_d3dback_entry_method(RT_BACK_Release) == NULL && rt_d3dback_entry_method(RT_BACK_CreateDevice) == NULL);

    T_CASE("rule 3: the device and its context have back objects of their kinds, asked for once");
    T_EQ(rt_d3dback_record_kind(rt_d3dback_of(device)), RT_BK_DEVICE);
    T_EQ(rt_d3dback_record_kind(rt_d3dback_of(context)), RT_BK_CONTEXT);
    T_EQ(rt_d3dback_record_count(RT_BACK_CreateDevice), 1);
    T_EQ(rt_d3dback_asked(RT_BACK_CreateDevice), 1);
    T_TRUE(rt_d3dback_of(NULL) == NULL);

    T_CASE("rule 7: a Get, a format query and the feature level are the front's alone");
    uint64_t before = reached_total();
    ID3D11Buffer *got[2] = { NULL, NULL };
    UINT support = 0;
    COM_CALL(ID3D11DeviceContext, VSGetConstantBuffers, context, 0, 2, got);
    T_EQ(COM_CALL(ID3D11Device, GetFeatureLevel, device), D3D_FEATURE_LEVEL_11_0);
    T_EQ(COM_CALL(ID3D11Device, CheckFormatSupport, device, DXGI_FORMAT_R8G8B8A8_UNORM, &support), S_OK);
    T_EQ(reached_total(), before);

    T_CASE("rule 2: the recording back end refuses what is not its own, and a front object with no back object cannot cross");
    ID3D11RenderTargetView *view = rt_nulldev_new(device, &rt_nulldev_rtv_class);       /* made without its creator: no back object */
    T_TRAPS(b->ClearRenderTargetView(rt_d3dback_of(context), view, colour), "is not an object of the back end");
    T_TRAPS(b->ClearRenderTargetView(rt_d3dback_of(context), rt_d3dback_of(device), colour), "which the entry does not take there");
    T_TRAPS((void)rt_d3dback_of(view), "has no back object although the back end record is there");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, view, colour), "has no back object although the back end record is there");
    T_TRAPS((void)rt_d3dback_of(((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter), "has no back object (rt/nulldev.h");
    void *obj = rt_d3dback_record_object(RT_BK_RTV);
    b->Release(RT_BK_RTV, obj);
    T_EQ(rt_d3dback_record_kind(obj), -1);
    T_TRAPS(b->ClearRenderTargetView(rt_d3dback_of(context), obj, colour), "which was released");
    T_TRAPS(b->Release(RT_BK_RTV, obj), "which was released");
    T_TRAPS(b->Release(RT_BK_BUFFER, rt_d3dback_of(context)), "which the entry does not take there");

    T_CASE("rule 1: a fatal report says whether the call that stopped had reached the back end");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, NULL, colour), "d3dback: the call that stopped had not reached the back end");
    rt_com_watch(&rt_iface_ID3D11DeviceContext, "ClearRenderTargetView", forward_too_early);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, NULL, colour),
            "d3dback: RULE BROKEN: the call that stopped (ID3D11DeviceContext::ClearRenderTargetView) had reached the back end already (entry ClearRenderTargetView)");

    T_CASE("the choice is made before the first device");
    T_TRAPS(rt_d3dback_use(NULL), "a device was already made with the back end record");
    rt_d3dback_use(rt_d3dback_record());                /* the same choice again is no change */

    T_CASE("rule 3: when the front objects go, every back object is released once");
    D3D11_BUFFER_DESC bd = { .ByteWidth = 32, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER };
    ID3D11Buffer *buffer = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buffer), S_OK);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIFactory1 *factory = ((NdAdapter *)rt_com_state_of(adapter, &rt_nulldev_adapter_class))->factory;
    DXGI_SWAP_CHAIN_DESC sd;
    memset(&sd, 0, sizeof sd);
    sd.BufferDesc.Width = 64; sd.BufferDesc.Height = 32; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = 0x20; sd.BufferCount = 2; sd.OutputWindow = (HWND)(uintptr_t)0x10042;
    sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain *sc = NULL;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &sd, &sc), S_OK);
    T_EQ(rt_d3dback_record_live(RT_BK_SWAPCHAIN), 1);
    T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE2D), 1);   /* the back buffer */
    rt_com_release(view);                               /* the one object that never had a back object */
    T_EQ(COM_CALL(IDXGISwapChain, Release, sc), 0);
    T_EQ(rt_d3dback_record_live(RT_BK_SWAPCHAIN), 0);
    T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE2D), 0);
    T_EQ(COM_CALL(ID3D11Buffer, Release, buffer), 0);
    COM_CALL(ID3D11DeviceContext, Release, context);
    COM_CALL(ID3D11Device, Release, device);
    uint64_t made = 0, live = 0;
    for (unsigned k = 0; k < RT_BK_KINDS; k++) { made += rt_d3dback_record_made((RtD3dBackKind)k); live += rt_d3dback_record_live((RtD3dBackKind)k); }
    T_EQ(live, 0);
    T_EQ(made, 6);                                      /* device, context, buffer, swap chain, back buffer, and the object made by hand */
    T_EQ(rt_d3dback_record_count(RT_BACK_Release), 6);
    T_EQ(rt_d3dback_asked(RT_BACK_Release), 5);         /* the front released its five; the sixth was released by hand above */
}

/* RT_D3D_BACKEND, read when the first device is made. */
TEST_SETUP(core, d3dback_env_record) { setenv("RT_D3D_BACKEND", "record", 1); }
TEST(core, d3dback_env_record)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    T_TRUE(rt_d3dback() == NULL);                       /* not chosen before the first device */
    rt_nulldev_create(&device, &context);
    T_TRUE(rt_d3dback() == rt_d3dback_record());
    T_TRUE(T_LOGGED(RT_CH_COM, "back end record behind the device"));
}

TEST_SETUP(core, d3dback_env_none) { unsetenv("RT_D3D_BACKEND"); }
TEST(core, d3dback_env_none)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    T_CASE("the default is none: no table, no back object, and the seam counts nothing");
    T_TRUE(rt_d3dback() == NULL);
    T_TRUE(((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->back == NULL);
    T_TRUE(((NdContext *)rt_com_state_of(context, &rt_nulldev_context_class))->child.back == NULL);
    COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 3, 0, 0);
    T_EQ(rt_d3dback_asked(RT_BACK_DrawIndexed), 0);
    T_EQ(reached_total(), 0);
    T_TRAPS(rt_d3dback_use(rt_d3dback_record()), "a device was already made with the back end none");
}

TEST_SETUP(core, d3dback_env_dxmt) { setenv("RT_D3D_BACKEND", "dxmt", 1); }
TEST(core, d3dback_env_dxmt)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    T_TRAPS(rt_nulldev_create(&device, &context), "RT_D3D_BACKEND=dxmt: this program was linked without the DXMT back end");
}

TEST_SETUP(core, d3dback_env_other) { setenv("RT_D3D_BACKEND", "metal", 1); }
TEST(core, d3dback_env_other)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    T_TRAPS(rt_nulldev_create(&device, &context), "RT_D3D_BACKEND=metal: the values are none (the default), record and dxmt");
}

/* A run of the game registers three report functions of its own before the device is made
 * (gate3/run.c: the network, the stand-ins, the input script); the device adds its ledger and
 * the seam its own.  With room for four the fifth was fatal in every run (found by this unit's
 * headless run, 12 October).  Expected: no fatal report, and the seam's line in the table. */
static void also_1(void) { }
static void also_2(void) { }
static void also_3(void) { }
TEST(core, d3dback_report_room)
{
    rt_com_counts_also(also_1);
    rt_com_counts_also(also_2);
    rt_com_counts_also(also_3);
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_BUFFER_DESC bd = { .ByteWidth = 32, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER };
    ID3D11Buffer *buffer = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateBuffer, device, &bd, NULL, &buffer), S_OK);     /* the ledger's totals register */
    COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 3, 0, 0);                    /* the guard's line registers */
    uint64_t collisions = 9;
    T_EQ(rt_nulldev_guard_stats(device, &collisions), 1);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, ClearRenderTargetView, context, NULL, NULL), "d3d: the context was called from 1 thread:");
}
