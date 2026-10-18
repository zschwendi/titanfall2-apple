/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkformatsupport
 * RT-CASES:  a measured format gives its bits and S_OK; a format the device refused gives E_INVALIDARG and 0, as measured; a format that was not measured is a stop
 * RT-TRAPS:  CheckFormatSupport for format; CheckFormatSupport with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Returns S_OK if successful; otherwise, returns E_INVALIDARG if the
 * Format parameter is NULL, or returns E_FAIL if the described format does not exist".  Wine's
 * test_format_support (conformance test d3d11.c) saw E_FAIL with 0 written for every format the
 * device does not support, format 0 and ~0u included, and no E_INVALIDARG.
 *
 * WHAT IS ANSWERED IS THE MEASUREMENT, NOT THOSE TWO (the planner's answer C to gate3-run-4):
 * the native renderer on this Mac, asked about every format number from 1 to 132
 * (tools/dxmt/format_probe.out; rt_nulldev_format_measured), answered S_OK with support bits
 * for 98 of them and E_INVALIDARG with 0 written for the other 34 (66, 89, 100 to 114 and 116
 * to 132).  That is this device's answer where Windows' own device says E_FAIL: a game that
 * tells the two apart would see the difference, and the game's four questions in a start
 * (42, 57, 41, 16) are all among the 98.  Format 0 and numbers above 132 were not measured:
 * a stop.  No source says what Windows returns for a NULL pointer: a stop too. */
COM_METHOD(ID3D11Device, HRESULT, CheckFormatSupport, DXGI_FORMAT Format, UINT *pFormatSupport)
{
    UINT bits = 0;
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (!pFormatSupport) RT_UNIMPLEMENTED(ctx, "CheckFormatSupport with a NULL out-pointer");
    HRESULT hr = S_OK;
    if (!rt_nulldev_format_measured(Format, &hr, &bits)) RT_UNIMPLEMENTED(ctx, "CheckFormatSupport for format %u", Format);
    *pFormatSupport = bits;             /* 0 with E_INVALIDARG, as the device wrote it */
    return hr;
}
