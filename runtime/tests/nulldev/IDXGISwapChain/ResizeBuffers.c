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

TEST(IDXGISwapChain, ResizeBuffers)
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
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, dxgi_device, &d, &sc), S_OK);
    const NdSwapChain *s = rt_com_state_of(sc, &rt_nulldev_swapchain_class);
    ID3D11Texture2D *first = s->back_buffer, *held = NULL;
    uint64_t serial = rt_com_serial(first);
    LONG dev0 = rt_com_refs(device);

    T_CASE("the game's call, ResizeBuffers(0, 1920, 1080, R8G8B8A8_UNORM_SRGB, 2): S_OK, the buffer count kept, a new back buffer");
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 1920, 1080, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 2), S_OK);
    T_EQ(s->desc.BufferCount, 3);
    T_EQ(s->desc.BufferDesc.Width, 1920);
    T_EQ(s->desc.BufferDesc.Format, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    T_EQ(s->desc.Flags, 2);
    T_TRUE(rt_com_serial(s->back_buffer) != serial);
    T_EQ(rt_com_refs(s->back_buffer), 1);
    T_EQ(rt_com_refs(device), dev0);                    /* the old buffer let go of the device */

    T_CASE("a new size and format: the description and the new back buffer have them");
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 2, 320, 240, DXGI_FORMAT_B8G8R8A8_UNORM, 0), S_OK);
    T_EQ(s->desc.BufferCount, 2);
    T_EQ(s->desc.BufferDesc.Width, 320);
    T_EQ(s->desc.BufferDesc.Height, 240);
    T_EQ(s->desc.BufferDesc.Format, 87);
    T_EQ(s->desc.Flags, 0);
    const NdTexture *t = rt_com_state_of(s->back_buffer, &rt_nulldev_texture2d_class);
    T_EQ(t->desc.d2.Width, 320);
    T_EQ(t->desc.d2.Height, 240);
    T_EQ(t->desc.d2.Format, 87);
    T_EQ(t->mem.size, 307200);                          /* 320 * 240 * 4 */

    T_CASE("format UNKNOWN keeps the format");
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 640, 480, DXGI_FORMAT_UNKNOWN, 0), S_OK);
    T_EQ(s->desc.BufferDesc.Format, 87);
    T_EQ(s->desc.BufferDesc.Width, 640);
    T_EQ(s->desc.BufferCount, 2);

    T_CASE("while the back buffer is referenced: DXGI_ERROR_INVALID_CALL and nothing changes");
    T_EQ(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&held), S_OK);
    T_EQ((uint32_t)COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 2, 320, 240, DXGI_FORMAT_B8G8R8A8_UNORM, 0), 0x887a0001u);
    T_EQ(s->desc.BufferDesc.Width, 640);
    T_EQ(s->desc.BufferDesc.Height, 480);
    T_TRUE(s->back_buffer == held);
    T_EQ(rt_com_release(held), 1);
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 2, 320, 240, DXGI_FORMAT_B8G8R8A8_UNORM, 0), S_OK);
    T_EQ(s->desc.BufferDesc.Width, 320);

    T_CASE("a width and height of 0 are the output window's client size, each by itself");
    /* ResizeBuffers' page as the body's comment has it (a 0 means the client area of the target
     * window); the window is a 640 x 480 popup, the swap chain was made 1920 x 1080. */
    HWND win = sc_window(u"ResizeBuffersWin", 640, 480);
    T_REQUIRE(win != NULL);
    IDXGISwapChain *z = NULL;
    DXGI_SWAP_CHAIN_DESC zd = d;
    zd.OutputWindow = win;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, dxgi_device, &zd, &z), S_OK);
    const NdSwapChain *zs = rt_com_state_of(z, &rt_nulldev_swapchain_class);
    T_EQ(zs->desc.BufferDesc.Width, 1920);
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, z, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0), S_OK);
    T_EQ(zs->desc.BufferDesc.Width, 640);
    T_EQ(zs->desc.BufferDesc.Height, 480);
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, z, 0, 0, 100, DXGI_FORMAT_UNKNOWN, 0), S_OK);
    T_EQ(zs->desc.BufferDesc.Width, 640);
    T_EQ(zs->desc.BufferDesc.Height, 100);
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, z, 0, 200, 0, DXGI_FORMAT_UNKNOWN, 0), S_OK);
    T_EQ(zs->desc.BufferDesc.Width, 200);
    T_EQ(zs->desc.BufferDesc.Height, 480);

    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0), "a 0 means the window's client area");
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 17, 320, 240, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers to 17 buffers");
    IDXGISwapChain *bare = rt_nulldev_new(device, &rt_nulldev_swapchain_class);
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeBuffers, bare, 0, 320, 240, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers of a swap chain that has no back buffer");
}
