/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-setstretchbltmode
 * RT-CASES:  returns the previous mode and sets the new one; a mode outside BLACKONWHITE to HALFTONE returns 0 with ERROR_INVALID_PARAMETER and changes nothing; a handle that is no context returns 0 with ERROR_INVALID_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, INT, SetStretchBltMode, HDC hdc, INT mode)
{
    /* The core refuses a mode outside 1 to 4 (ERROR_INVALID_PARAMETER) and a handle that is
     * not a context (ERROR_INVALID_HANDLE), and sets last error itself (rt/gdi.h, rt_dc_attr).
     * The value is passed as ULONG, so a negative mode is refused too. */
    ULONG v = (ULONG)mode, old;
    return rt_dc_attr(hdc, RT_DC_STRETCH_MODE, &v, &old) ? (INT)old : 0;
}
