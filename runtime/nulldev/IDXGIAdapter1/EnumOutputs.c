/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiadapter-enumoutputs
 * RT-CASES:  index 0 is the one output, a new object with one reference; index 1 and above is DXGI_ERROR_NOT_FOUND
 * RT-TRAPS:  EnumOutputs with a NULL out-pointer
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "DXGI_ERROR_NOT_FOUND is returned if the index is greater than
 * the number of outputs" (the page's own example loops until that code, so "greater than or
 * equal" is meant); on success "EnumOutputs increments the output interface's reference
 * count"; index 0 is "the output on which the desktop primary is displayed".  The page names
 * no result for a NULL ppOutput: a stop.
 *
 * How many outputs: 1, the desktop of rt/window.h (design-null-device.md 3.4: "one output
 * with the desktop numbers of the window contract's rt_desktop"; the native renderer also
 * reported one, finding 35, docs/findings/35-dxmt-window.md section 4).  NULL in the
 * out-pointer on "not found": as IDXGIFactory1/EnumAdapters1.c. */
COM_METHOD(IDXGIAdapter1, HRESULT, EnumOutputs, UINT output_idx, IDXGIOutput **output)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_adapter_class);
    if (!output) RT_UNIMPLEMENTED(ctx, "EnumOutputs with a NULL out-pointer (index %u)", output_idx);
    if (output_idx >= ND_OUTPUT_COUNT) {
        *output = NULL;
        return DXGI_ERROR_NOT_FOUND;
    }
    *output = rt_nulldev_output_new(This);
    return S_OK;
}
