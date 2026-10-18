/* Expected values.  The description is the one the game builds (finding 31, section 2; the
 * reference trace as the orchestrator reports it): 1920 by 1080, refresh 0/0, format 29
 * (R8G8B8A8_UNORM_SRGB), one sample, usage 0x70, 3 buffers, windowed, DISCARD, flags 2; then
 * ResizeBuffers(0, 1920, 1080, 29, 2) before the first GetBuffer.  Microsoft's pages of
 * CreateSwapChain, GetBuffer and ResizeBuffers (a NULL description is DXGI_ERROR_INVALID_CALL;
 * buffer 0; count 0 and format UNKNOWN keep what there is) and Wine's dxgi test
 * test_swapchain_resize as the contract's check reads it (docs/reviews/d3d-contract-check.md,
 * E7: a referenced back buffer makes ResizeBuffers answer 0x887a0001 and change nothing).  The
 * back buffer's size in bytes is by hand: 1920 * 1080 * 4 = 8294400. */
#include <string.h>

#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/nulldev.h"

/* A window of this runtime for the cases with a width or height of 0: a popup has no frame, so
 * its client area is its size (rt/window.h, rt_window_frame). */
GUEST_FN(sc_win_proc)
{
    const HWND h = (HWND)(uintptr_t)ctx->r[1];
    const UINT msg = (UINT)ctx->r[2];
    const WPARAM wp = ctx->r[8];
    const LPARAM lp = (LPARAM)ctx->r[9];
    tf_fp_enter_host();
    const LRESULT r = rt_window_def_proc(ctx, h, msg, wp, lp, true);
    tf_fp_enter_guest(ctx->mxcsr);
    ctx->r[0] = (uint64_t)r;
    GUEST_RET(ctx);
}
static HWND sc_window(const WCHAR *cls, INT cx, INT cy)
{
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(sc_win_proc), .lpszClassName = cls };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(cls), .title = RT_TEXT_W(u"t"), .x = 0, .y = 0, .cx = cx, .cy = cy, .unicode = true };
    return rt_window_create(T_CTX(), &a);
}

TEST(IDXGIFactory1, CreateSwapChain)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIFactory1 *factory = ((NdAdapter *)rt_com_state_of(adapter, &rt_nulldev_adapter_class))->factory;
    IUnknown *dxgi_device = rt_com_face(device, &rt_iface_IDXGIDevice1);
    DXGI_SWAP_CHAIN_DESC d;
    memset(&d, 0, sizeof d);
    d.BufferDesc.Width = 1920; d.BufferDesc.Height = 1080; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    d.SampleDesc.Count = 1;
    d.BufferUsage = 0x70;                               /* SHADER_INPUT | RENDER_TARGET_OUTPUT | BACK_BUFFER */
    d.BufferCount = 3;
    d.OutputWindow = (HWND)(uintptr_t)0x10042;
    d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    d.Flags = 2;                                        /* ALLOW_MODE_SWITCH */
    IDXGISwapChain *sc = NULL;
    DXGI_SWAP_CHAIN_DESC bad;
    LONG dev0 = rt_com_refs(device), fac0 = rt_com_refs(factory);

    T_CASE("the game's description: S_OK, the description kept, the device and the factory held, a back buffer of the description's size and format");
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &d, &sc), S_OK);
    T_REQUIRE(sc != NULL);
    const NdSwapChain *s = rt_com_state_of(sc, &rt_nulldev_swapchain_class);
    T_TRUE(!memcmp(&s->desc, &d, sizeof d));
    T_TRUE(s->device == device);
    T_TRUE(s->factory == factory);
    T_EQ(s->fullscreen, FALSE);
    T_EQ(rt_com_refs(sc), 1);
    T_EQ(rt_com_refs(factory), fac0 + 1);
    T_EQ(rt_com_refs(device), dev0 + 2);                /* the swap chain and its back buffer */
    T_REQUIRE(s->back_buffer != NULL);
    T_EQ(rt_com_refs(s->back_buffer), 1);
    const NdTexture *t = rt_com_state_of(s->back_buffer, &rt_nulldev_texture2d_class);
    T_EQ(t->desc.d2.Width, 1920);
    T_EQ(t->desc.d2.Height, 1080);
    T_EQ(t->desc.d2.MipLevels, 1);
    T_EQ(t->desc.d2.ArraySize, 1);
    T_EQ(t->desc.d2.Format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    T_EQ(t->desc.d2.SampleDesc.Count, 1);
    T_EQ(t->desc.d2.Usage, D3D11_USAGE_DEFAULT);
    T_EQ(t->desc.d2.BindFlags, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    T_EQ(t->mem.size, 8294400);

    T_CASE("the DXGI device face is accepted as the device");
    IDXGISwapChain *second = NULL;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, dxgi_device, &d, &second), S_OK);
    T_TRUE(((NdSwapChain *)rt_com_state_of(second, &rt_nulldev_swapchain_class))->device == device);

    T_CASE("a NULL description or out-pointer is DXGI_ERROR_INVALID_CALL");
    T_EQ((uint32_t)COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, NULL, &sc), 0x887a0001u);
    T_EQ((uint32_t)COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &d, NULL), 0x887a0001u);

    T_CASE("a width and height of 0 are the output window's client size, each by itself, and the description keeps the resolved size");
    /* DXGI_MODE_DESC's page as the body's comment has it (a 0 is taken from the output window and
     * GetDesc gives the assigned value); the window is a 640 x 480 popup. */
    HWND win = sc_window(u"CreateSwapChainWin", 640, 480);
    T_REQUIRE(win != NULL);
    IDXGISwapChain *z = NULL;
    DXGI_SWAP_CHAIN_DESC zd = d;
    zd.OutputWindow = win; zd.BufferDesc.Width = 0; zd.BufferDesc.Height = 0;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &zd, &z), S_OK);
    const NdSwapChain *zs = rt_com_state_of(z, &rt_nulldev_swapchain_class);
    T_EQ(zs->desc.BufferDesc.Width, 640);
    T_EQ(zs->desc.BufferDesc.Height, 480);
    T_EQ(((const NdTexture *)rt_com_state_of(zs->back_buffer, &rt_nulldev_texture2d_class))->desc.d2.Width, 640);
    T_EQ(((const NdTexture *)rt_com_state_of(zs->back_buffer, &rt_nulldev_texture2d_class))->desc.d2.Height, 480);
    T_EQ(zd.BufferDesc.Width, 0);                       /* the caller's description is read, not written */
    zd.BufferDesc.Width = 0; zd.BufferDesc.Height = 100;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &zd, &z), S_OK);
    zs = rt_com_state_of(z, &rt_nulldev_swapchain_class);
    T_EQ(zs->desc.BufferDesc.Width, 640);
    T_EQ(zs->desc.BufferDesc.Height, 100);
    zd.BufferDesc.Width = 200; zd.BufferDesc.Height = 0;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &zd, &z), S_OK);
    zs = rt_com_state_of(z, &rt_nulldev_swapchain_class);
    T_EQ(zs->desc.BufferDesc.Width, 200);
    T_EQ(zs->desc.BufferDesc.Height, 480);

    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)adapter, &d, &sc), "that is not a device of this layer");
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, NULL, &d, &sc), "that is not a device of this layer");
    bad = d; bad.BufferDesc.Width = 0;
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &bad, &sc), "a 0 means the window's client size");
    bad = d; bad.BufferDesc.Height = 0;
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &bad, &sc), "a 0 means the window's client size");
    bad = d; bad.OutputWindow = NULL;
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &bad, &sc), "CreateSwapChain with a NULL output window");
    bad = d; bad.Windowed = FALSE;
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &bad, &sc), "CreateSwapChain for full screen");
    bad = d; bad.SwapEffect = 3;                        /* FLIP_SEQUENTIAL */
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &bad, &sc), "CreateSwapChain with swap effect 3");
    bad = d; bad.BufferCount = 0;
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &bad, &sc), "CreateSwapChain with 0 buffers");
    bad = d; bad.BufferDesc.Format = 103;               /* DXGI_FORMAT_NV12 */
    T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &bad, &sc), "a format that has no size in the format table");
}
