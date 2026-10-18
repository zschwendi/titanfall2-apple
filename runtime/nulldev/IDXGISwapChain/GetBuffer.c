/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getbuffer
 * RT-CASES:  buffer 0 as ID3D11Texture2D: S_OK, the back buffer with one more reference; twice gives the same object
 * RT-TRAPS:  GetBuffer with riid; GetBuffer of buffer 1; GetBuffer for an interface other than ID3D11Texture2D; GetBuffer of a swap chain that has no back buffer
 */
#include <string.h>

#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10 by a subagent, which paraphrased it: for DXGI_SWAP_EFFECT_DISCARD
 * only the first buffer is accessible, index 0.  The game asks for buffer 0 as ID3D11Texture2D
 * (materialsystem_dx11+0x15e54, +0x15ea4: finding 31).  That the pointer comes with one more
 * reference is COM's rule for an out-parameter (from memory).  Stops: any other buffer
 * (Windows: DXGI_ERROR_INVALID_CALL, Wine's dxgi tests through docs/reviews/
 * d3d-contract-check.md; the contract keeps it a stop), any other interface, a NULL pointer,
 * and a swap chain record that CreateSwapChain did not make. */
COM_METHOD(IDXGISwapChain, HRESULT, GetBuffer, UINT buffer_idx, REFIID riid, void **surface)
{
    NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    if (!riid || !surface) RT_UNIMPLEMENTED(ctx, "GetBuffer with riid %p and out-pointer %p", (const void *)riid, (void *)surface);
    if (buffer_idx) RT_UNIMPLEMENTED(ctx, "GetBuffer of buffer %u: only buffer 0 is served", (unsigned)buffer_idx);
    if (memcmp(riid, &rt_iface_ID3D11Texture2D.iid, sizeof *riid))
        RT_UNIMPLEMENTED(ctx, "GetBuffer for an interface other than ID3D11Texture2D: {%08x-%04x-%04x-...}", (unsigned)riid->Data1,
                         (unsigned)riid->Data2, (unsigned)riid->Data3);
    if (!s->back_buffer) RT_UNIMPLEMENTED(ctx, "GetBuffer of a swap chain that has no back buffer");
    *surface = rt_nulldev_ref(s->back_buffer);
    return S_OK;
}
