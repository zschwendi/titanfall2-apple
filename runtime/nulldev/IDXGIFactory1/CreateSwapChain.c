/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createswapchain
 * RT-CASES:  the game's description: S_OK, the description kept, the device and the factory held, a back buffer of the description's size and format; the DXGI device face is accepted as the device; a NULL description or out-pointer is DXGI_ERROR_INVALID_CALL; a width and height of 0 are the output window's client size, each by itself, and the description keeps the resolved size
 * RT-TRAPS:  that is not a device of this layer; a 0 means the window's client size; CreateSwapChain with a NULL output window; CreateSwapChain for full screen; CreateSwapChain with swap effect; buffers; a format that has no size in the format table
 */
#include "rt/nulldev.h"
#include "rt/window.h"

/* The swap chain record keeps the description as passed, the factory, the device and one back
 * buffer: a texture 2D of BufferDesc's width, height and format with the description's
 * SampleDesc, one mip level, one slice (rt_nulldev_swapchain_buffer).  THE WINDOW IS ASKED
 * ONE THING: its client size, when a width or height of 0 is passed, which mean "the
 * window's client size" (DXGI_MODE_DESC's page: the runtime takes the size from the output
 * window, and GetDesc returns the assigned value); the resolved size is what is kept in
 * NdSwapChain.desc (docs/reviews/d3d-contract-check.md, point 3).  Otherwise OutputWindow is
 * recorded as the number it is.
 *
 * RT-DOC and DXGI_SWAP_CHAIN_DESC, read 2026-10-10 by a subagent, which paraphrased them:
 * DXGI_ERROR_INVALID_CALL when pDesc or ppSwapChain is NULL; pDevice is, for Direct3D 11, a
 * pointer to the Direct3D device and cannot be NULL (the game passes the device's
 * IDXGIDevice1 face: finding 31); OutputWindow must not be NULL.  S_OK for success is from
 * memory.  Stops, where the page names no result or this device has no answer: a pDevice
 * that is not our device; full screen at creation (decision D1 of the contract; the game
 * creates windowed and switches afterwards); a swap effect other than DISCARD (the one the
 * game passes; GetBuffer serves buffer 0 only, which is DISCARD's rule); 0 buffers or more
 * than 16 (DXGI_MAX_SWAP_CHAIN_BUFFERS; from memory). */
COM_METHOD(IDXGIFactory1, HRESULT, CreateSwapChain, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapchain)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_factory_class);
    if (!desc || !swapchain) {
        RT_BACK_ALONE(ctx, CreateSwapChain);    /* the front's own failure: the back end is not asked (rt/d3dback.h, rule 1) */
        return DXGI_ERROR_INVALID_CALL;
    }
    if (!device || rt_com_class_of(device) != &rt_nulldev_device_class)
        RT_UNIMPLEMENTED(ctx, "CreateSwapChain with a device (%p) that is not a device of this layer", (void *)device);
    if (!desc->OutputWindow) RT_UNIMPLEMENTED(ctx, "CreateSwapChain with a NULL output window");
    /* A width or height of 0 is the output window's client size (unit gate3-run-3, with the
     * window record's rt_window_client_size), each of the two by itself; a handle that is no
     * window, and a client area of 0, stay stops. */
    UINT width = desc->BufferDesc.Width, height = desc->BufferDesc.Height, cw = 0, ch = 0;
    if (!width || !height) {
        if (!rt_window_client_size(desc->OutputWindow, &cw, &ch) || !cw || !ch)
            RT_UNIMPLEMENTED(ctx, "CreateSwapChain with width %u and height %u: a 0 means the window's client size, and %p is no window "
                             "or has a client area of %u x %u", (unsigned)width, (unsigned)height, (void *)desc->OutputWindow,
                             (unsigned)cw, (unsigned)ch);
        if (!width) width = cw;
        if (!height) height = ch;
    }
    if (!desc->Windowed) RT_UNIMPLEMENTED(ctx, "CreateSwapChain for full screen (Windowed FALSE)");
    if (desc->SwapEffect != DXGI_SWAP_EFFECT_DISCARD)
        RT_UNIMPLEMENTED(ctx, "CreateSwapChain with swap effect %u; only DISCARD (0) is built", (unsigned)desc->SwapEffect);
    if (!desc->BufferCount || desc->BufferCount > 16) RT_UNIMPLEMENTED(ctx, "CreateSwapChain with %u buffers", (unsigned)desc->BufferCount);
    IDXGISwapChain *sc = rt_nulldev_new(rt_com_face(device, &rt_iface_ID3D11Device), &rt_nulldev_swapchain_class);
    NdSwapChain *s = rt_com_state(ctx, sc, &rt_nulldev_swapchain_class);
    s->desc = *desc;
    s->desc.BufferDesc.Width = width;       /* the resolved size is what GetDesc gives back (DXGI_MODE_DESC's page) */
    s->desc.BufferDesc.Height = height;
    rt_nulldev_bind(&s->factory, This);
    /* The seam (rt/d3dback.h, rules 3 and 4): the back end's swap chain, from the description as
     * the front keeps it (a width or height of 0 already resolved), before the back buffer,
     * which rt_nulldev_swapchain_buffer asks the back end's swap chain for. */
    RT_BACK_CREATE(ctx, sc, CreateSwapChain, rt_d3dback_of(device), &s->desc);
    rt_nulldev_swapchain_buffer(ctx, sc);
    *swapchain = sc;
    return S_OK;
}
