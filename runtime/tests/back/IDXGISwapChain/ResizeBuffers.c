/* The forward of IDXGISwapChain::ResizeBuffers through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the swap chain crosses as its back
 * object; the numbers cross as the front resolved them (a buffer count of 0 as the count kept, a
 * format UNKNOWN as the format kept).  Rule 3: the old back buffer's back object is released once,
 * before the back end resizes (a renderer refuses to resize while a buffer is referenced), and the
 * new back buffer is asked for from the back end's swap chain (SwapChainGetBuffer).  Rule 7: a
 * ResizeBuffers refused for a referenced back buffer (DXGI_ERROR_INVALID_CALL) is the front's own
 * answer: the back end is not asked.  Rule 4: a result of the back end that differs from S_OK is a
 * stop that names the method.  Rule 1: a count above 16 stops before the back end is reached. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(IDXGISwapChain, ResizeBuffers)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
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
    d.Flags = 2;
    IDXGISwapChain *sc = NULL;
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &d, &sc), S_OK);
    NdSwapChain *s = rt_com_state_of(sc, &rt_nulldev_swapchain_class);
    uint64_t old = T_BACK ? T_BACK_OF(s->back_buffer) : 0;
    uint64_t releases = T_BACK ? T_BACK_COUNT(Release) : 0;

    T_CASE("the game's call: S_OK; the back end gets the front's resolved numbers, and the old back buffer's back object is released first");
    T_EQ(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 640, 360, DXGI_FORMAT_UNKNOWN, 0), S_OK);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ResizeBuffers), 1);
        T_EQ(T_BACK_ARG(ResizeBuffers, 0), T_BACK_OF(sc));
        T_EQ(T_BACK_ARG(ResizeBuffers, 1), d.BufferCount);
        T_EQ(T_BACK_ARG(ResizeBuffers, 2), 640);
        T_EQ(T_BACK_ARG(ResizeBuffers, 3), 360);
        T_EQ(T_BACK_ARG(ResizeBuffers, 4), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        T_EQ(T_BACK_ARG(ResizeBuffers, 5), 0);
        T_EQ(T_BACK_COUNT(Release), releases + 1);
        T_EQ(T_BACK_ARG(Release, 1), old);
        T_EQ(T_BACK_COUNT(SwapChainGetBuffer), 2);
        T_TRUE(T_BACK_OF(s->back_buffer) != old && T_BACK_OF(s->back_buffer) != 0);
        T_EQ(rt_d3dback_record_live(RT_BK_TEXTURE2D), 1);
    }

    T_CASE("a referenced back buffer: DXGI_ERROR_INVALID_CALL, and the back end is not asked");
    ID3D11Texture2D *bb = NULL;
    T_EQ(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&bb), S_OK);
    T_REQUIRE(bb != NULL);
    T_EQ((uint32_t)COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 320, 240, DXGI_FORMAT_UNKNOWN, 0), 0x887a0001u);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(ResizeBuffers), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_ResizeBuffers), 1);
    }
    /* the game's Release returns the application's count alone: 0, the swap chain's hold keeps the buffer alive (rt/nulldev.h) */
    T_EQ(COM_CALL(ID3D11Texture2D, Release, bb), 0);

    if (T_BACK) {
        T_CASE("a result of the back end that differs from S_OK is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_ResizeBuffers, E_FAIL);
        T_TRAPS(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 320, 240, DXGI_FORMAT_UNKNOWN, 0),
                "the back end (record) answered 0x80004005 to IDXGISwapChain::ResizeBuffers");
        rt_d3dback_record_answer(RT_BACK_ResizeBuffers, S_OK);
    }

    T_CASE("17 buffers stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 17, 640, 360, DXGI_FORMAT_UNKNOWN, 0));
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGISwapChain, ResizeBuffers, sc, 0, 0, 360, DXGI_FORMAT_UNKNOWN, 0));
}
