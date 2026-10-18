/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createdcw
 * RT-CASES:  display context for DISPLAY with no device; driver names compared without regard to ASCII case; DISPLAY with any device; the adapter's name as driver, or as device with no driver; NULL for no driver and no device, for a NULL driver with device DISPLAY, and for a printer; last error unchanged
 * RT-TRAPS:  CreateDCW with pszPort; CreateDCW with pdm; core: an index rt_desktop_device_cap does not answer; core: DeleteDC of a context that GetDC gave out
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HDC, CreateDCW, LPCWSTR pwszDriver, LPCWSTR pwszDevice, LPCWSTR pszPort, LPCVOID pdm)
{
    /* The documentation says pszPort is ignored and must be NULL, and that pdm must be NULL
     * for the default initialisation; a non-NULL value is not implemented here. */
    if (pszPort) RT_UNIMPLEMENTED(ctx, "CreateDCW with pszPort %p", (const void *)pszPort);
    if (pdm) RT_UNIMPLEMENTED(ctx, "CreateDCW with pdm %p", pdm);
    return rt_dc_create_display(pwszDriver, pwszDevice);
}
