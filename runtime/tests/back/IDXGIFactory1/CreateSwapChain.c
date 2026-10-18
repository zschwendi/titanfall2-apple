/* The forward of IDXGIFactory1::CreateSwapChain through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the device crosses as its back object,
 * and the description crosses by pointer to the front's own copy (NdSwapChain.desc, with the
 * resolved size), never to the game's.  Rule 3: the swap chain gets a back object from the
 * creator's out-pointer, and the back buffer is asked for once from the back end's swap chain
 * (SwapChainGetBuffer, buffer 0), whose result becomes the back object of the front's back buffer.
 * Rule 7: GetBuffer is the front's alone, and a call that fails its own check (a NULL description:
 * DXGI_ERROR_INVALID_CALL) is answered by the front alone.  Rule 4: a result of the back end that
 * differs from the front's S_OK is a stop that names the method.  Rule 1: a device that is not a
 * device of this layer stops before the back end is reached. */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(IDXGIFactory1, CreateSwapChain)
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
    d.BufferUsage = 0x70;                               /* SHADER_INPUT | RENDER_TARGET_OUTPUT | BACK_BUFFER */
    d.BufferCount = 3;
    d.OutputWindow = (HWND)(uintptr_t)0x10042;
    d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    d.Flags = 2;                                        /* ALLOW_MODE_SWITCH */
    IDXGISwapChain *sc = NULL, *second = NULL;

    T_CASE("the game's description: S_OK; the back end makes the swap chain from the front's copy of the description, by pointer");
    T_EQ(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &d, &sc), S_OK);
    T_REQUIRE(sc != NULL);
    const NdSwapChain *s = rt_com_state_of(sc, &rt_nulldev_swapchain_class);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateSwapChain), 1);
        T_EQ(T_BACK_ARG(CreateSwapChain, 0), T_BACK_OF(device));
        T_TRUE(T_BACK_ARG(CreateSwapChain, 1) != T_BACK_PTR(&d));
        T_EQ(T_BACK_ARG(CreateSwapChain, 1), T_BACK_PTR(&((NdSwapChain *)rt_com_state_of(sc, &rt_nulldev_swapchain_class))->desc));
        T_EQ(T_BACK_ARG(CreateSwapChain, 2), T_BACK_OF(sc));
        T_TRUE(T_BACK_OF(sc) != 0);
        T_EQ(T_BACK_COUNT(SwapChainGetBuffer), 1);
        T_EQ(T_BACK_ARG(SwapChainGetBuffer, 0), T_BACK_OF(sc));
        T_EQ(T_BACK_ARG(SwapChainGetBuffer, 1), 0);
        T_EQ(T_BACK_ARG(SwapChainGetBuffer, 2), T_BACK_OF(s->back_buffer));
        T_TRUE(T_BACK_OF(s->back_buffer) != 0);
    }

    T_CASE("GetBuffer is the front's alone: the back end is not asked again");
    ID3D11Texture2D *bb = NULL;
    T_EQ(COM_CALL(IDXGISwapChain, GetBuffer, sc, 0, &rt_iface_ID3D11Texture2D.iid, (void **)&bb), S_OK);
    T_REQUIRE(bb != NULL);
    if (T_BACK) T_EQ(T_BACK_COUNT(SwapChainGetBuffer), 1);

    T_CASE("a NULL description is DXGI_ERROR_INVALID_CALL in both modes, and the front answers alone");
    IDXGISwapChain *bad_sc = NULL;
    T_EQ((uint32_t)COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, NULL, &bad_sc), 0x887a0001u);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(CreateSwapChain), 1);
        T_EQ(rt_d3dback_alone(RT_BACK_CreateSwapChain), 1);
    }

    if (T_BACK) {
        T_CASE("a result of the back end that differs from the front's S_OK is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_CreateSwapChain, E_FAIL);
        T_TRAPS(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &d, &second),
                "the back end (record) answered 0x80004005 to IDXGIFactory1::CreateSwapChain");
        rt_d3dback_record_answer(RT_BACK_CreateSwapChain, S_OK);
    }

    T_CASE("a device that is not a device of this layer stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)context, &d, &second));

    DXGI_SWAP_CHAIN_DESC e1 = d;
    e1.OutputWindow = NULL;
    IDXGISwapChain *o1 = NULL;
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &e1, &o1));
    DXGI_SWAP_CHAIN_DESC e2 = d;
    e2.Windowed = FALSE;
    IDXGISwapChain *o2 = NULL;
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &e2, &o2));
    DXGI_SWAP_CHAIN_DESC e3 = d;
    e3.SwapEffect = 1;
    IDXGISwapChain *o3 = NULL;
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &e3, &o3));
    DXGI_SWAP_CHAIN_DESC e4 = d;
    e4.BufferCount = 0;
    IDXGISwapChain *o4 = NULL;
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &e4, &o4));
    DXGI_SWAP_CHAIN_DESC e5 = d;
    e5.BufferCount = 17;
    IDXGISwapChain *o5 = NULL;
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGIFactory1, CreateSwapChain, factory, (IUnknown *)device, &e5, &o5));
}
