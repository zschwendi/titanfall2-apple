/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgioutput-findclosestmatchingmode
 * RT-CASES:  format 28 gives the desktop's mode in the format asked for, and S_OK
 * RT-TRAPS:  FindClosestMatchingMode for format; FindClosestMatchingMode with a NULL mode
 */
#include "rt/nulldev.h"
#include "rt/window.h"

/* RT-DOC: the page of IDXGIOutput::FindClosestMatchingMode was NOT read (no network fetch
 * available here); the address above is the lower-case form. From memory only: device may be
 * NULL when the format is given, and format 0 takes the format from the device.
 *
 * THE ONE MODE is the desktop's (design-null-device.md 3.4; GetDisplayModeList.c says why), so
 * every request answers with it:
 *   Width, Height     the desktop's            RefreshRate   refresh_hz / 1
 *   Format            mode->Format             ScanlineOrdering  DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE
 *   Scaling           DXGI_MODE_SCALING_UNSPECIFIED
 * Answered only for DXGI_FORMAT_R8G8B8A8_UNORM (the one format the mode list answers for).
 * Any other format is a stop (format 0 would take the device's format, which is not built).
 * A NULL mode or closest_match is a stop, with the message the contract gives. */
COM_METHOD(IDXGIOutput, HRESULT, FindClosestMatchingMode, const DXGI_MODE_DESC *mode, DXGI_MODE_DESC *closest_match, IUnknown *device)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_output_class);
    (void)device;
    if (!mode || !closest_match) RT_UNIMPLEMENTED(ctx, "FindClosestMatchingMode with a NULL mode");
    if (mode->Format != DXGI_FORMAT_R8G8B8A8_UNORM) RT_UNIMPLEMENTED(ctx, "FindClosestMatchingMode for format %u", mode->Format);
    const RtDesktop *d = rt_desktop();
    closest_match->Width = (UINT)d->width;
    closest_match->Height = (UINT)d->height;
    closest_match->RefreshRate.Numerator = d->refresh_hz;
    closest_match->RefreshRate.Denominator = 1;
    closest_match->Format = mode->Format;
    closest_match->ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    closest_match->Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    return S_OK;
}
