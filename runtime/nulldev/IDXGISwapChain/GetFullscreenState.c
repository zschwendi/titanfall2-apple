/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getfullscreenstate
 * RT-CASES:  GetFullscreenState writes FALSE and NULL over other values and moves no reference; each out-pointer may be NULL, and the other is still written
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: pFullscreen and ppTarget are both "[out, optional]"; ppTarget is
 * the output "when the mode is full screen; otherwise NULL", and its reference count is
 * incremented only in full-screen mode, so a windowed swap chain writes no reference.  The
 * page lists no return code; S_OK is from the notes.  This swap chain is always windowed
 * (finding 31, section 2): FALSE and NULL. */
COM_METHOD(IDXGISwapChain, HRESULT, GetFullscreenState, BOOL *fullscreen, IDXGIOutput **target)
{
    NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    if (fullscreen) *fullscreen = s->fullscreen;
    if (target) *target = NULL;
    return S_OK;
}
