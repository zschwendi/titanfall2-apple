/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-setbkmode
 * RT-CASES:  returns the previous mode and sets the new one; a mode other than TRANSPARENT and OPAQUE returns 0 and changes nothing; a handle that is no context returns 0 with ERROR_INVALID_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, INT, SetBkMode, HDC hdc, INT mode)
{
    /* The core refuses a mode other than TRANSPARENT and OPAQUE, and a handle that is not a
     * context, and sets last error itself for the handle (rt/gdi.h, rt_dc_attr). */
    ULONG v = (ULONG)mode, old;
    return rt_dc_attr(hdc, RT_DC_BK_MODE, &v, &old) ? (INT)old : 0;
}
