/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkmultisamplequalitylevels
 * RT-CASES:  format 28 with 1, 2 and 4 samples gives S_OK and 1; format 28 with 8, 16, 3, 15 and 32 samples gives S_OK and 0; format 45 with 4 samples and format 40 with 4 samples give 1; format 71 with 2 samples gives S_OK and 0, with 1 sample 1; format 42 with 4 samples gives S_OK and 0; sample count 0 and 33 give E_FAIL and 0; a NULL pointer gives E_INVALIDARG, with 2 samples or with 0 samples
 * RT-TRAPS:  CheckMultisampleQualityLevels for format
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "When the combination is not supported, this method returns a failure
 * HRESULT code (that is, a negative integer), or sets pNumQualityLevels output parameter to zero,
 * or both."  Wine's test_check_multisample_quality_levels (conformance test d3d11.c): NULL with
 * 0, 1 and 2 samples gives E_INVALIDARG, so the pointer is looked at before the count; 0, 33 and
 * 64 samples give E_FAIL and nothing else; BC3_UNORM with 2 samples gives S_OK and 0.
 *
 * The native renderer answers by the sample count alone (third_party/dxmt
 * src/d3d11/d3d11_device.cpp, CheckMultisampleQualityLevels1); here the format's measured bits
 * decide 2 and 4 samples, as Windows does (the BC formats and R32_UINT lack the bit, finding 35
 * section 3).  The bits are those of the 98 formats the device has (rt_nulldev_format_support:
 * the whole column of tools/dxmt/format_probe.out).  For the 34 numbers the device refused in
 * CheckFormatSupport, for format 0 and for numbers above 132 this method was never measured:
 * a stop with the format. */
COM_METHOD(ID3D11Device, HRESULT, CheckMultisampleQualityLevels, DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels)
{
    UINT bits = 0;
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (!rt_nulldev_format_support(Format, &bits)) RT_UNIMPLEMENTED(ctx, "CheckMultisampleQualityLevels for format %u", Format);
    if (!pNumQualityLevels) return E_INVALIDARG;
    if (SampleCount == 0 || SampleCount > 32) {
        *pNumQualityLevels = 0;
        return E_FAIL;
    }
    if (SampleCount == 1) {
        *pNumQualityLevels = 1;
    } else if (SampleCount == 2 || SampleCount == 4) {
        *pNumQualityLevels = (bits & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) ? 1 : 0;
    } else {
        *pNumQualityLevels = 0;
    }
    return S_OK;
}
