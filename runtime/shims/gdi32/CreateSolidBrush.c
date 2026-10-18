/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createsolidbrush
 * RT-CASES:  creates a solid brush of the colour given; each call gives a new handle
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HBRUSH, CreateSolidBrush, COLORREF color)
{
    /* The core's brush (rt/gdi.h, rt_gdi_create_brush); it sets nothing on failure. */
    return rt_gdi_create_brush(color);
}
