/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgioutput-getdisplaymodelist
 * RT-CASES:  a NULL list returns the number of modes, 1; a list with room receives the desktop's mode and the count 1; a list with no room is DXGI_ERROR_MORE_DATA and receives nothing
 * RT-TRAPS:  GetDisplayModeList for format; GetDisplayModeList with flags; GetDisplayModeList with a NULL count
 */
#include "rt/nulldev.h"
#include "rt/window.h"

/* RT-DOC, read 2026-10-10.  pNumModes: "Set pDesc to NULL so that pNumModes returns the
 * number of display modes that match the format and the options.  Otherwise, pNumModes
 * returns the number of display modes returned in pDesc."  Return value: one of the DXGI
 * codes; "DXGI_ERROR_MORE_DATA is returned (if there is not enough room for all the display
 * modes)".
 *
 * THE LIST IS THE CONTRACT'S CHOICE: one mode, the desktop's own (rt_desktop(), the window
 * contract's numbers from finding 30's trace: 1920 by 1080 at 60 Hz), so that DXGI and
 * GetSystemMetrics agree (design-null-device.md 3.4).  The native renderer listed 132 modes
 * of a display this runtime does not have (finding 35); they are not copied.
 *   Width, Height     the desktop's            RefreshRate   refresh_hz / 1
 *   Format            the format asked for     ScanlineOrdering  DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE
 *   Scaling           DXGI_MODE_SCALING_UNSPECIFIED
 *
 * Answered only for what the game asks (finding 31, section 5: format 28, flags 0; a run
 * passed exactly that at materialsystem_dx11+0x117b0): DXGI_FORMAT_R8G8B8A8_UNORM with no
 * flags.  Any other format or flag is a stop: which formats have modes, and what the two
 * flags add, has not been decided.
 *
 * With too little room the page says only which code is returned.  Here the list and the
 * count are left as they were: with one mode, "too little room" is a count of 0, and 0 modes
 * were returned (the same as Wine's dxgi does for it; from memory). */
COM_METHOD(IDXGIOutput, HRESULT, GetDisplayModeList, DXGI_FORMAT format, UINT flags, UINT *mode_count, DXGI_MODE_DESC *desc)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_output_class);
    if (format != DXGI_FORMAT_R8G8B8A8_UNORM) RT_UNIMPLEMENTED(ctx, "GetDisplayModeList for format %u", format);
    if (flags) RT_UNIMPLEMENTED(ctx, "GetDisplayModeList with flags %#x", flags);
    if (!mode_count) RT_UNIMPLEMENTED(ctx, "GetDisplayModeList with a NULL count");
    if (!desc) {
        *mode_count = 1;
        return S_OK;
    }
    if (*mode_count < 1) return DXGI_ERROR_MORE_DATA;
    const RtDesktop *d = rt_desktop();
    desc->Width = (UINT)d->width;
    desc->Height = (UINT)d->height;
    desc->RefreshRate.Numerator = d->refresh_hz;
    desc->RefreshRate.Denominator = 1;
    desc->Format = format;
    desc->ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
    desc->Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    *mode_count = 1;
    return S_OK;
}
