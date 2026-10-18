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
#include "rt/nulldev.h"

TEST(IDXGISwapChain, GetBuffer)
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
    ID3D11Texture2D *bb = NULL, *again = NULL;

    T_CASE("buffer 0 as ID3D11Texture2D: S_OK, the back buffer with one more reference");
    T_EQ(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&bb), S_OK);
    T_TRUE(bb == s->back_buffer);
    T_EQ(rt_com_refs(bb), 2);
    T_TRUE(rt_com_class_of(bb) == &rt_nulldev_texture2d_class);

    T_CASE("twice gives the same object");
    T_EQ(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&again), S_OK);
    T_TRUE(again == bb);
    T_EQ(rt_com_refs(bb), 3);

    T_TRAPS(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, NULL, (void **)&bb), "GetBuffer with riid");
    T_TRAPS(COM_CALL(IDXGISwapChain, GetBuffer, sc, 1, &rt_iface_ID3D11Texture2D.iid, (void **)&bb), "GetBuffer of buffer 1");
    T_TRAPS(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_IDXGISurface.iid, (void **)&bb), "GetBuffer for an interface other than ID3D11Texture2D");
    IDXGISwapChain *bare = rt_nulldev_new(device, &rt_nulldev_swapchain_class);
    T_TRAPS(COM_CALL(IDXGISwapChain, GetBuffer, bare, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&bb), "GetBuffer of a swap chain that has no back buffer");
}
