/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-setrectrgn
 * RT-CASES:  sets the rectangle of a region handle; a brush and NULL fail
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, BOOL, SetRectRgn, HRGN hrgn, INT left, INT top, INT right, INT bottom)
{
    return rt_gdi_set_rect_rgn(hrgn, left, top, right, bottom);
}
