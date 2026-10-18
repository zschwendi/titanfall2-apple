/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgioutput-getdesc
 * RT-CASES:  the one output is the desktop of rt/window.h, every byte of the structure
 * RT-TRAPS:  GetDesc with a NULL description
 */
#include <string.h>

#include "rt/nulldev.h"
#include "rt/window.h"

/* Fills all 96 bytes of DXGI_OUTPUT_DESC (layout: rt/win/dxgi.h) and returns S_OK.
 *
 *   DeviceName          "\\.\DISPLAY1", zero-filled to 32 units: what the native renderer
 *                       reported (finding 35, docs/findings/35-dxmt-window.md section 4)
 *   DesktopCoordinates  rt_desktop()->monitor: the one monitor of the window contract, so that
 *                       GetSystemMetrics, EnumDisplayMonitors and DXGI agree
 *                       (design-null-device.md 3.4).  NOT the native renderer's 1512 by 982
 *   AttachedToDesktop   TRUE (the native renderer's answer, the same line)
 *   Rotation            DXGI_MODE_ROTATION_IDENTITY (1): an unrotated desktop.  Contract's choice;
 *                       the probe did not print it
 *   Monitor             RT_HMONITOR_PRIMARY, the window contract's handle of that monitor
 *
 * A NULL description: Microsoft's page gives E_INVALIDARG for it (from memory, not read
 * today), so it is a stop until someone reads the page. */
COM_METHOD(IDXGIOutput, HRESULT, GetDesc, DXGI_OUTPUT_DESC *desc)
{
    static const WCHAR name[] = ND_OUTPUT_DEVICE_NAME;
    (void)rt_com_state(ctx, This, &rt_nulldev_output_class);
    if (!desc) RT_UNIMPLEMENTED(ctx, "GetDesc with a NULL description");
    memset(desc, 0, sizeof *desc);
    memcpy(desc->DeviceName, name, sizeof name);
    desc->DesktopCoordinates = rt_desktop()->monitor;
    desc->AttachedToDesktop = TRUE;
    desc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
    desc->Monitor = RT_HMONITOR_PRIMARY;
    return S_OK;
}
