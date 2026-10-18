/* Expected values: S_OK for the swap chain's own mode is the planner's decision (brief of unit
 * gate3-run-3) and what the recorded real start was answered for exactly that call (finding
 * 40, calls 5387 and 5390; the mode is the recording's: 1920 x 1080, refresh 0/0, format 29
 * R8G8B8A8_UNORM_SRGB, scanline order and scaling 0).  That nothing changes: Microsoft's page
 * (read 2026-10-10: the call resizes the target window; "only ResizeBuffers can change the
 * back buffers").  Every other mode is a stop by the same decision. */
#include <string.h>

#include "rt/test.h"
#include "rt/shim.h"
#include "rt/nulldev.h"
#include "rt/window.h"

GUEST_FN(rt_proc)
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

TEST(IDXGISwapChain, ResizeTarget)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGISwapChain *sc = rt_nulldev_new(device, &rt_nulldev_swapchain_class);
    NdSwapChain *s = rt_com_state_of(sc, &rt_nulldev_swapchain_class);
    const DXGI_MODE_DESC game = { .Width = 1920, .Height = 1080, .RefreshRate = { 0, 0 }, .Format = 29, .ScanlineOrdering = 0, .Scaling = 0 };
    DXGI_SWAP_CHAIN_DESC before;
    DXGI_MODE_DESC m;

    T_CASE("the swap chain's own mode, field for field, is S_OK and changes nothing");
    s->desc.BufferDesc = game;
    s->desc.BufferCount = 3;
    s->desc.OutputWindow = (HWND)(uintptr_t)0x30156;        /* a number that is no window of this runtime */
    s->desc.Windowed = TRUE;
    before = s->desc;
    m = game;
    T_EQ(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), S_OK);
    T_EQ(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), S_OK);     /* the recording calls it twice */
    T_TRUE(memcmp(&s->desc, &before, sizeof before) == 0);
    T_TRUE(memcmp(&m, &game, sizeof m) == 0);
    T_EQ(rt_com_refs(sc), 1);
    T_EQ(s->fullscreen, FALSE);

    T_CASE("the same when the output window is a window whose client area has the mode's size");
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(rt_proc), .lpszClassName = u"ResizeTargetWin" };
    rt_wndclass_register(&wc, true);
    RtWindowCreate a = { .style = WS_POPUP, .cls = RT_TEXT_W(u"ResizeTargetWin"), .title = RT_TEXT_W(u"t"), .x = 0, .y = 0, .cx = 640, .cy = 480,
                         .unicode = true };
    HWND w = rt_window_create(T_CTX(), &a);                 /* a popup has no frame: the client area is 640 x 480 */
    T_REQUIRE(w != NULL);
    s->desc.OutputWindow = w;
    s->desc.BufferDesc.Width = 640;
    s->desc.BufferDesc.Height = 480;
    before = s->desc;
    m = s->desc.BufferDesc;
    T_EQ(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), S_OK);
    T_TRUE(memcmp(&s->desc, &before, sizeof before) == 0);

    /* The window's client area is 640 x 480 and the swap chain's own mode is now 1920 x 1080:
     * the call would have to resize the window. */
    s->desc.BufferDesc = game;
    m = game;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m),
            "ResizeTarget with the swap chain's own mode while its window's client area is 640 x 480, not the mode's 1920 x 1080");

    /* Every field that differs is another mode; so is a format of UNKNOWN. */
    s->desc.OutputWindow = (HWND)(uintptr_t)0x30156;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, NULL), "ResizeTarget with a NULL mode");
    m = game; m.Width = 1280;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), "ResizeTarget to another mode than the swap chain's: 1280 x 1080");
    m = game; m.Height = 720;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), "ResizeTarget to another mode than the swap chain's: 1920 x 720");
    m = game; m.RefreshRate.Numerator = 60;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), "ResizeTarget to another mode than the swap chain's: 1920 x 1080, refresh 60/0");
    m = game; m.RefreshRate.Denominator = 1;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), "ResizeTarget to another mode than the swap chain's: 1920 x 1080, refresh 0/1");
    m = game; m.Format = 0;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), "refresh 0/0, format 0,");
    m = game; m.ScanlineOrdering = 1;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), "format 29, scanline order 1,");
    m = game; m.Scaling = 2;
    T_TRAPS(COM_CALL(IDXGISwapChain, ResizeTarget, sc, &m), "scanline order 0, scaling 2 (");
}
