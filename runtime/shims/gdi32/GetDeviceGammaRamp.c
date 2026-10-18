/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-getdevicegammaramp
 * RT-CASES:  FALSE with a 1536-byte ramp buffer untouched; NULL and non-NULL device context; does not change last error; logs its line once
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(gdi32, BOOL, GetDeviceGammaRamp, HDC hdc, LPVOID lpRamp)
{
    /* Not every display supports a loadable gamma ramp, and this one does not: the documented
     * failure, FALSE. The ramp buffer is neither read nor written, and last error is left alone. */
    (void)hdc;
    (void)lpRamp;
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "gdi32.dll!GetDeviceGammaRamp", "GetDeviceGammaRamp: the display has no such feature");
    return FALSE;
}
