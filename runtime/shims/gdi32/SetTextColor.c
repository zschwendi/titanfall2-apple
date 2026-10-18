/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-settextcolor
 * RT-CASES:  sets the text colour of a context and returns the one before; a colour is stored as given; a handle that is no context gives CLR_INVALID with ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, COLORREF, SetTextColor, HDC hdc, COLORREF color)
{
    /* The core stores the colour as given and returns the one before (rt/gdi.h, rt_dc_attr);
     * it sets ERROR_INVALID_HANDLE itself for a handle that is not a context. */
    ULONG v = color, old;
    return rt_dc_attr(hdc, RT_DC_TEXT_COLOR, &v, &old) ? old : CLR_INVALID;
}
