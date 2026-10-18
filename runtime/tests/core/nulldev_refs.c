/* The reference counts the game can see on a resource (unit d3d-fix-2; F3 of
 * docs/reviews/d3d-contract-check-2.md, whose test this is).
 *
 * Expected values: Wine's d3d11 conformance tests as that block reads them
 * (dlls/d3d11/tests/d3d11.c): test_create_rendertarget_view, test_create_shader_resource_view
 * and test_create_depthstencil_view read a texture's count before and after creating each view
 * and find it equal; test_swapchain_views finds the back buffer's count 1 after GetBuffer, and
 * still 1 after two render-target views are made on it and one is bound with
 * OMSetRenderTargets.  That the resource stays alive while a view or a binding exists:
 * Microsoft's page of IDXGISwapChain::ResizeBuffers, which calls views and bindings "indirect
 * references" (read by the same block).  That a device's count rises with each object it
 * makes: Wine's creator tests (the same block). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(core, nulldev_refs)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdContext *c = rt_com_state_of(context, &rt_nulldev_context_class);
    D3D11_TEXTURE2D_DESC td = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *t = NULL;
    ID3D11RenderTargetView *r = NULL;
    ID3D11ShaderResourceView *sv = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateTexture2D, device, &td, NULL, &t), S_OK);

    T_CASE("a new texture: AddRef returns 2, Release 1");
    T_EQ(COM_CALL(ID3D11Texture2D, AddRef, t), 2);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, t), 1);

    T_CASE("a view of it leaves the texture's count as it was, and the device's count rises by one");
    LONG dev0 = (LONG)COM_CALL(ID3D11Device, AddRef, device);
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)t, NULL, &r), S_OK);
    T_EQ(COM_CALL(ID3D11Texture2D, AddRef, t), 2);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, t), 1);
    T_EQ(COM_CALL(ID3D11Device, CreateShaderResourceView, device, (ID3D11Resource *)t, NULL, &sv), S_OK);
    T_EQ(COM_CALL(ID3D11Texture2D, AddRef, t), 2);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, t), 1);
    T_EQ(COM_CALL(ID3D11Device, AddRef, device), (ULONG)dev0 + 3);     /* two views, and this AddRef */
    COM_CALL(ID3D11Device, Release, device);
    COM_CALL(ID3D11Device, Release, device);
    T_EQ(rt_com_refs(t), 3);                            /* what keeps it alive: the game and two views */

    T_CASE("binding the view leaves the texture's count and the view's own as they were");
    rt_nulldev_bind(&c->rtv[0], r);                     /* what OMSetRenderTargets does with it */
    T_EQ(COM_CALL(ID3D11Texture2D, AddRef, t), 2);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, t), 1);
    T_EQ(COM_CALL(ID3D11RenderTargetView, AddRef, r), 2);
    T_EQ(COM_CALL(ID3D11RenderTargetView, Release, r), 1);

    T_CASE("the game's last Release of the texture returns 0 and the views keep it alive");
    T_EQ(COM_CALL(ID3D11Texture2D, Release, t), 0);
    T_TRUE(rt_com_class_of(t) == &rt_nulldev_texture2d_class);
    T_EQ(COM_CALL(ID3D11ShaderResourceView, Release, sv), 0);
    T_TRUE(rt_com_class_of(sv) == NULL);
    T_TRUE(rt_com_class_of(t) == &rt_nulldev_texture2d_class);

    T_CASE("the bound view outlives the game's Release, and the texture goes with it when it is unbound");
    T_EQ(COM_CALL(ID3D11RenderTargetView, Release, r), 0);
    T_TRUE(rt_com_class_of(r) == &rt_nulldev_rtv_class);
    T_TRUE(rt_com_class_of(t) == &rt_nulldev_texture2d_class);
    rt_nulldev_bind(&c->rtv[0], NULL);
    T_TRUE(rt_com_class_of(r) == NULL);
    T_TRUE(rt_com_class_of(t) == NULL);

    T_CASE("the swap chain's buffer: count 1 after GetBuffer, Release returns 0, and a later GetBuffer gives the same object");
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIFactory1 *factory = ((NdAdapter *)rt_com_state_of(adapter, &rt_nulldev_adapter_class))->factory;
    DXGI_SWAP_CHAIN_DESC d;
    memset(&d, 0, sizeof d);
    d.BufferDesc.Width = 1920; d.BufferDesc.Height = 1080; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    d.SampleDesc.Count = 1;
    d.BufferUsage = 0x70;
    d.BufferCount = 3;
    d.OutputWindow = (HWND)(uintptr_t)0x10042;
    d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain *sc = NULL;
    ID3D11Texture2D *bb = NULL, *again = NULL;
    ID3D11RenderTargetView *v1 = NULL, *v2 = NULL;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, rt_com_face(device, &rt_iface_IDXGIDevice1), &d, &sc), S_OK);
    T_EQ(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&bb), S_OK);
    T_EQ(COM_CALL(ID3D11Texture2D, AddRef, bb), 2);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, bb), 1);
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)bb, NULL, &v1), S_OK);
    T_EQ(COM_CALL(ID3D11Device, CreateRenderTargetView, device, (ID3D11Resource *)bb, NULL, &v2), S_OK);
    rt_nulldev_bind(&c->rtv[0], v1);
    T_EQ(COM_CALL(ID3D11Texture2D, AddRef, bb), 2);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, bb), 1);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, bb), 0);
    T_EQ(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&again), S_OK);
    T_TRUE(again == bb);
    T_EQ(COM_CALL(ID3D11Texture2D, AddRef, bb), 2);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, bb), 1);

    T_CASE("ResizeBuffers counts both kinds: refused while a view holds the buffer, also when the game holds none");
    T_EQ(COM_CALL(ID3D11Texture2D, Release, bb), 0);
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 1280, 720, DXGI_FORMAT_UNKNOWN, 0), DXGI_ERROR_INVALID_CALL);
    rt_nulldev_bind(&c->rtv[0], NULL);
    T_EQ(COM_CALL(ID3D11RenderTargetView, Release, v1), 0);
    T_EQ(COM_CALL(ID3D11RenderTargetView, Release, v2), 0);
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 1280, 720, DXGI_FORMAT_UNKNOWN, 0), S_OK);
    T_TRUE(rt_com_class_of(bb) == NULL);                /* the old buffer went with the swap chain's hold */
}
