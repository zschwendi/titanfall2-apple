/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createrectrgn
 * RT-CASES:  returns a handle to a region for a rectangle; an empty rectangle gives a region too (the empty region)
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HRGN, CreateRectRgn, INT x1, INT y1, INT x2, INT y2)
{
    return rt_gdi_create_rect_rgn(x1, y1, x2, y2);
}
