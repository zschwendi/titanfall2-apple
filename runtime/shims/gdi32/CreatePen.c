/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createpen
 * RT-CASES:  creates a pen of style, width and colour; a negative width reads back as its absolute value; PS_NULL pen reads back as the null pen; each call gives a new handle
 * RT-TRAPS:  core: style outside 0 to 6
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HPEN, CreatePen, INT iStyle, INT cWidth, COLORREF color)
{
    /* The core's pen (rt/gdi.h, rt_gdi_create_pen).  It sets last error itself when it fails
     * and a style outside 0 to 6 is a stop of the core (its comment), so the shim adds no
     * guard for it. */
    return rt_gdi_create_pen(iStyle, cWidth, color);
}
