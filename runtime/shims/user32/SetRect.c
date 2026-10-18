/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setrect
 * RT-CASES:  stores the four values in the RECT and returns TRUE; a NULL RECT returns FALSE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(user32, BOOL, SetRect, LPRECT lprc, INT xLeft, INT yTop, INT xRight, INT yBottom)
{
    if (!lprc) return FALSE;

    lprc->left = xLeft;
    lprc->top = yTop;
    lprc->right = xRight;
    lprc->bottom = yBottom;
    return TRUE;
}
