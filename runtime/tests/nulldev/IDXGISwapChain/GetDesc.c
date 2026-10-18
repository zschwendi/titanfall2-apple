/* Expected values: Microsoft's page of IDXGISwapChain::GetDesc (read 2026-10-11: the
 * description of the swap chain, returned by value into the caller's buffer); the
 * description the test fills in is the test's own choice, distinct in every field; the size
 * 72 is rt/win/dxgi.h's DXGI_SWAP_CHAIN_DESC; the stop for a NULL description is the contract's
 * choice (the page names no result). */
#include <string.h>
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGISwapChain, GetDesc)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGISwapChain *sc = rt_nulldev_new(device, &rt_nulldev_swapchain_class);
    NdSwapChain *s = rt_com_state_of(sc, &rt_nulldev_swapchain_class);
    s->desc.BufferDesc.Width = 1921;
    s->desc.BufferDesc.Height = 1082;
    s->desc.BufferDesc.RefreshRate.Numerator = 60001;
    s->desc.BufferDesc.RefreshRate.Denominator = 1003;
    s->desc.BufferDesc.Format = 28;
    s->desc.BufferDesc.ScanlineOrdering = 2;
    s->desc.BufferDesc.Scaling = 1;
    s->desc.SampleDesc.Count = 4;
    s->desc.SampleDesc.Quality = 7;
    s->desc.BufferUsage = 0x20;
    s->desc.BufferCount = 3;
    s->desc.OutputWindow = (HWND)(uintptr_t)0x5151;
    s->desc.Windowed = 0;
    s->desc.SwapEffect = 4;
    s->desc.Flags = 0x0b;

    unsigned char buf[72 + 8];
    memset(buf, 0xa5, sizeof buf);
    DXGI_SWAP_CHAIN_DESC *out = (DXGI_SWAP_CHAIN_DESC *)buf;

    T_CASE("the description the swap chain was created with (72 bytes written, nothing after them)");
    T_EQ(COM_CALL(IDXGISwapChain, GetDesc, sc, out), S_OK);
    T_EQ(memcmp(buf, &s->desc, 72), 0);
    T_EQ(out->BufferDesc.Width, 1921u);
    T_EQ(out->BufferDesc.Height, 1082u);
    T_EQ(out->BufferDesc.RefreshRate.Numerator, 60001u);
    T_EQ(out->BufferDesc.RefreshRate.Denominator, 1003u);
    T_EQ(out->BufferDesc.Format, 28u);
    T_EQ(out->BufferDesc.ScanlineOrdering, 2u);
    T_EQ(out->BufferDesc.Scaling, 1u);
    T_EQ(out->SampleDesc.Count, 4u);
    T_EQ(out->SampleDesc.Quality, 7u);
    T_EQ(out->BufferUsage, 0x20u);
    T_EQ(out->BufferCount, 3u);
    T_TRUE(out->OutputWindow == (HWND)(uintptr_t)0x5151);
    T_EQ(out->Windowed, 0);
    T_EQ(out->SwapEffect, 4u);
    T_EQ(out->Flags, 0x0bu);
    for (int i = 72; i < 80; i++) T_EQ(buf[i], 0xa5);
    T_EQ(rt_com_refs(sc), 1);

    T_CASE("a NULL description is a stop");
    T_TRAPS(COM_CALL(IDXGISwapChain, GetDesc, sc, NULL), "GetDesc with a NULL description");
}
