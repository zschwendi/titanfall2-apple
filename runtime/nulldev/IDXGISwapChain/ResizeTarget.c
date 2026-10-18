/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizetarget
 * RT-CASES:  the swap chain's own mode, field for field, is S_OK and changes nothing; the same when the output window is a window whose client area has the mode's size
 * RT-TRAPS:  ResizeTarget with a NULL mode; ResizeTarget to another mode than the swap chain's; ResizeTarget with the swap chain's own mode while its window's client area is
 */
#include "rt/nulldev.h"
#include "rt/window.h"

/* RT-DOC, read 2026-10-10: "Resizes the output target."  "ResizeTarget resizes the target
 * window when the swap chain is in windowed mode, and changes the display mode on the target
 * output when the swap chain is in full-screen mode."  "Apps must still call
 * IDXGISwapChain::ResizeBuffers after they call ResizeTarget because only ResizeBuffers can
 * change the back buffers."  The page names no success code but "a code that indicates
 * success or failure"; S_OK is what the recorded real start was answered (finding 40, calls
 * 5387 and 5390).
 *
 * THE PLANNER'S DECISION (brief of unit gate3-run-3): a mode equal to the swap chain's current
 * one answers S_OK, any other mode stays a stop.  This swap chain is always windowed, so the
 * call would resize the window; with the mode the swap chain already has there is nothing to
 * resize, and the game passes exactly that (materialsystem_dx11+0x155f8: the BufferDesc it
 * gave CreateSwapChain two calls earlier, as the recording has it).  "Equal" is every field:
 * a format of UNKNOWN, which the page says means the existing format, is not answered either.
 * One more stop, this body's own: when the output window is a window record and its client
 * area does not have the mode's size, S_OK with nothing done would leave a window the call
 * should have resized, so that stops too (a handle that is no window is not looked at: the
 * swap chain records the number it was given, CreateSwapChain.c). */
COM_METHOD(IDXGISwapChain, HRESULT, ResizeTarget, const DXGI_MODE_DESC *target_mode_desc)
{
    NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    const DXGI_MODE_DESC *m = target_mode_desc, *c = &s->desc.BufferDesc;
    UINT cw = 0, ch = 0;

    if (!m) RT_UNIMPLEMENTED(ctx, "ResizeTarget with a NULL mode");
    if (m->Width != c->Width || m->Height != c->Height || m->RefreshRate.Numerator != c->RefreshRate.Numerator ||
        m->RefreshRate.Denominator != c->RefreshRate.Denominator || m->Format != c->Format ||
        m->ScanlineOrdering != c->ScanlineOrdering || m->Scaling != c->Scaling)
        RT_UNIMPLEMENTED(ctx, "ResizeTarget to another mode than the swap chain's: %u x %u, refresh %u/%u, format %u, scanline order %u, "
                         "scaling %u (the swap chain has %u x %u, %u/%u, %u, %u, %u)",
                         (unsigned)m->Width, (unsigned)m->Height, (unsigned)m->RefreshRate.Numerator, (unsigned)m->RefreshRate.Denominator,
                         (unsigned)m->Format, (unsigned)m->ScanlineOrdering, (unsigned)m->Scaling,
                         (unsigned)c->Width, (unsigned)c->Height, (unsigned)c->RefreshRate.Numerator, (unsigned)c->RefreshRate.Denominator,
                         (unsigned)c->Format, (unsigned)c->ScanlineOrdering, (unsigned)c->Scaling);
    if (rt_window_client_size(s->desc.OutputWindow, &cw, &ch) && (cw != m->Width || ch != m->Height))
        RT_UNIMPLEMENTED(ctx, "ResizeTarget with the swap chain's own mode while its window's client area is %u x %u, not the mode's %u x %u",
                         (unsigned)cw, (unsigned)ch, (unsigned)m->Width, (unsigned)m->Height);
    return S_OK;
}
