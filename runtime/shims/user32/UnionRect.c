/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-unionrect
 * RT-CASES:  overlapping rectangles give their bounding box; rectangles touching at an edge give their bounding box; one rectangle inside the other gives the outer one; an empty source in either position is ignored; two empty sources give an empty destination and FALSE; the destination may be one of the sources; a NULL pointer in any position returns FALSE and leaves the destination alone; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(user32, BOOL, UnionRect, LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2)
{
    LONG left, top, right, bottom;
    BOOL empty1, empty2, ok;

    if (!lprcDst || !lprcSrc1 || !lprcSrc2) return FALSE;

    /* Both sources are read into locals first: lprcDst may be one of them. */
    empty1 = lprcSrc1->left >= lprcSrc1->right || lprcSrc1->top >= lprcSrc1->bottom;
    empty2 = lprcSrc2->left >= lprcSrc2->right || lprcSrc2->top >= lprcSrc2->bottom;
    if (empty1 && empty2) {
        left = top = right = bottom = 0;
        ok = FALSE;
    } else if (empty1) {
        left = lprcSrc2->left;
        top = lprcSrc2->top;
        right = lprcSrc2->right;
        bottom = lprcSrc2->bottom;
        ok = TRUE;
    } else if (empty2) {
        left = lprcSrc1->left;
        top = lprcSrc1->top;
        right = lprcSrc1->right;
        bottom = lprcSrc1->bottom;
        ok = TRUE;
    } else {
        left = lprcSrc1->left < lprcSrc2->left ? lprcSrc1->left : lprcSrc2->left;
        top = lprcSrc1->top < lprcSrc2->top ? lprcSrc1->top : lprcSrc2->top;
        right = lprcSrc1->right > lprcSrc2->right ? lprcSrc1->right : lprcSrc2->right;
        bottom = lprcSrc1->bottom > lprcSrc2->bottom ? lprcSrc1->bottom : lprcSrc2->bottom;
        ok = TRUE;
    }

    lprcDst->left = left;
    lprcDst->top = top;
    lprcDst->right = right;
    lprcDst->bottom = bottom;
    return ok;
}
