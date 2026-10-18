/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-gettextcolor
 * RT-CASES:  returns the text colour of a context; a handle that is no context gives CLR_INVALID with ERROR_INVALID_HANDLE; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, COLORREF, GetTextColor, HDC hdc)
{
    /* The core's text colour of the context (rt/gdi.h, rt_dc_attr); it sets
     * ERROR_INVALID_HANDLE itself for a handle that is not a context. */
    ULONG old;
    return rt_dc_attr(hdc, RT_DC_TEXT_COLOR, NULL, &old) ? old : CLR_INVALID;
}
