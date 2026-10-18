/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-movetoex
 * RT-CASES:  stores the current position and returns the previous one in the POINT; a NULL POINT is allowed; the current position and the viewport origin are independent; a handle that is no context fails
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, BOOL, MoveToEx, HDC hdc, INT x, INT y, LPPOINT lppt)
{
    /* The core's current position (rt/gdi.h, rt_dc_set_point): it stores the position, writes
     * the previous one to lppt when lppt is not NULL, and sets ERROR_INVALID_HANDLE itself for
     * a handle that is no context. */
    return rt_dc_set_point(hdc, RT_DC_POSITION, x, y, lppt);
}
