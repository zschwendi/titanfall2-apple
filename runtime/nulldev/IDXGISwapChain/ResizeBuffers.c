/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers
 * RT-CASES:  the game's call, ResizeBuffers(0, 1920, 1080, R8G8B8A8_UNORM_SRGB, 2): S_OK, the buffer count kept, a new back buffer; a new size and format: the description and the new back buffer have them; format UNKNOWN keeps the format; while the back buffer is referenced: DXGI_ERROR_INVALID_CALL and nothing changes; a width and height of 0 are the output window's client size, each by itself
 * RT-TRAPS:  a 0 means the window's client area; ResizeBuffers to; ResizeBuffers of a swap chain that has no back buffer
 */
#include "rt/nulldev.h"
#include "rt/window.h"

/* RT-DOC, read 2026-10-10 by a subagent, which paraphrased it: BufferCount 0 keeps the
 * existing count; NewFormat DXGI_FORMAT_UNKNOWN keeps the existing format; a width or height
 * of 0 means the client area of the target window (taken from the window record, as in
 * CreateSwapChain; a handle that is no window stops); resizing requires that every reference to the back buffers has been
 * released, direct or through a view or a binding.  What Windows answers when one has not
 * (Wine's dxgi test test_swapchain_resize, through docs/reviews/d3d-contract-check.md, E7):
 * DXGI_ERROR_INVALID_CALL, and GetDesc still gives the old size.  Here "referenced" is a count
 * above the swap chain's own one: a view of the back buffer holds it, and so does the game.
 * The game calls this right after CreateSwapChain and before its first GetBuffer (the
 * reference trace, as the orchestrator reports it), so its start does not meet the refusal.
 * That the description's Flags become SwapChainFlags is from memory.  More than 16 buffers
 * (DXGI_MAX_SWAP_CHAIN_BUFFERS; from memory): a stop. */
COM_METHOD(IDXGISwapChain, HRESULT, ResizeBuffers, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    if (!s->back_buffer) RT_UNIMPLEMENTED(ctx, "ResizeBuffers of a swap chain that has no back buffer");
    if (rt_com_refs(s->back_buffer) > 1) {
        RT_BACK_ALONE(ctx, ResizeBuffers);      /* the front's own failure: the back end is not asked (rt/d3dback.h, rule 1) */
        return DXGI_ERROR_INVALID_CALL;
    }
    if (!width || !height) {
        /* A 0 is the output window's client size (unit gate3-run-3; rt_window_client_size),
         * each of the two by itself; a handle that is no window, and a client area of 0, stop. */
        UINT cw = 0, ch = 0;
        if (!rt_window_client_size(s->desc.OutputWindow, &cw, &ch) || !cw || !ch)
            RT_UNIMPLEMENTED(ctx, "ResizeBuffers with width %u and height %u: a 0 means the window's client area, and %p is no window "
                             "or has a client area of %u x %u", (unsigned)width, (unsigned)height, (void *)s->desc.OutputWindow,
                             (unsigned)cw, (unsigned)ch);
        if (!width) width = cw;
        if (!height) height = ch;
    }
    if (buffer_count > 16) RT_UNIMPLEMENTED(ctx, "ResizeBuffers to %u buffers", (unsigned)buffer_count);
    if (buffer_count) s->desc.BufferCount = buffer_count;
    s->desc.BufferDesc.Width = width;
    s->desc.BufferDesc.Height = height;
    if (format != DXGI_FORMAT_UNKNOWN) s->desc.BufferDesc.Format = format;
    s->desc.Flags = flags;
    /* The seam (rt/d3dback.h, rules 3 and 4).  A renderer refuses to resize while a buffer of the
     * swap chain is referenced, so the old back buffer goes first, and its back object with it
     * (nothing but the swap chain held it: the check above); then the back end resizes, with
     * the numbers the front resolved; then the new back buffer is made and asked for. */
    if (rt_d3dback()) {
        ID3D11Texture2D *old = s->back_buffer;
        s->back_buffer = NULL;
        rt_com_unhold(old);
        RT_BACK_SAME(ctx, S_OK, ResizeBuffers, s->back, s->desc.BufferCount, width, height, s->desc.BufferDesc.Format, flags);
    }
    rt_nulldev_swapchain_buffer(ctx, This);
    return S_OK;
}
