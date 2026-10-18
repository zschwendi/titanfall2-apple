/* The forward of IDXGISwapChain::Present through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the swap chain crosses as its back
 * object.  Rule 6: with a back end the present, its pacing and its result are the back end's: the
 * front invents nothing (the ledger's ND_INV_PRESENT stays 0), a success code is handed to the game
 * as the back end gave it (DXGI_STATUS_OCCLUDED here), and a failure is a stop that names the
 * method (rule 4).  Rule 1: a sync interval above 4 and any flag stop before the back end is
 * reached.  The swap chain is made by CreateSwapChain, so that it has its back object (rule 3). */
#include <string.h>

#include "rt/test_back.h"

BACK_TEST(IDXGISwapChain, Present)
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
    T_REQUIRE(sc != NULL);

    T_CASE("a present is handed on once, with its numbers, and the front invents no answer");
    T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 0, 0), S_OK);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Present), 1);
        T_EQ(T_BACK_ARG(Present, 0), T_BACK_OF(sc));
        T_EQ(T_BACK_ARG(Present, 1), 0);
        T_EQ(T_BACK_ARG(Present, 2), 0);
        T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT), 0);
    } else {
        T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT), 1);
    }

    if (T_BACK) {
        T_CASE("the back end's status is the game's answer: a success code is handed on as it was given");
        rt_d3dback_record_answer(RT_BACK_Present, DXGI_STATUS_OCCLUDED);
        T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 1, 0), DXGI_STATUS_OCCLUDED);
        T_EQ(T_BACK_COUNT(Present), 2);
        T_EQ(T_BACK_ARG(Present, 1), 1);

        T_CASE("a failure of the back end is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_Present, E_FAIL);
        T_TRAPS(COM_CALL(IDXGISwapChain, Present, sc, 1, 0), "the back end (record) answered 0x80004005 to IDXGISwapChain::Present");
        rt_d3dback_record_answer(RT_BACK_Present, S_OK);
    }

    T_CASE("a sync interval above 4 and any flag stop before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGISwapChain, Present, sc, 5, 0));
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGISwapChain, Present, sc, 1, 1));
}
