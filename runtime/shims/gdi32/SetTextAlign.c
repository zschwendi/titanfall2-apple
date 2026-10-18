/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-settextalign
 * RT-CASES:  returns the previous alignment and sets the new one; a handle that is no context returns GDI_ERROR with ERROR_INVALID_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, UINT, SetTextAlign, HDC hdc, UINT align)
{
    /* The core sets last error itself for a handle that is not a context (rt/gdi.h,
     * rt_dc_attr); the failure value of the alignment is GDI_ERROR, 0xFFFFFFFF. */
    ULONG v = (ULONG)align, old;
    return rt_dc_attr(hdc, RT_DC_TEXT_ALIGN, &v, &old) ? (UINT)old : 0xFFFFFFFFu;
}
