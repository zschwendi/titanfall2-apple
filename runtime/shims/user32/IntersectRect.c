/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-intersectrect
 * RT-CASES:  overlapping rectangles give their common part; rectangles touching at an edge do not intersect; one rectangle inside the other gives the inner one; an empty source in either position gives FALSE and an empty destination; the destination may be one of the sources; a NULL pointer in any position returns FALSE and leaves the destination alone; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(user32, BOOL, IntersectRect, LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2)
{
    LONG left, top, right, bottom;
    BOOL empty;

    if (!lprcDst || !lprcSrc1 || !lprcSrc2) return FALSE;

    /* The result is computed into locals first: lprcDst may be one of the sources. */
    left = lprcSrc1->left > lprcSrc2->left ? lprcSrc1->left : lprcSrc2->left;
    top = lprcSrc1->top > lprcSrc2->top ? lprcSrc1->top : lprcSrc2->top;
    right = lprcSrc1->right < lprcSrc2->right ? lprcSrc1->right : lprcSrc2->right;
    bottom = lprcSrc1->bottom < lprcSrc2->bottom ? lprcSrc1->bottom : lprcSrc2->bottom;

    empty = lprcSrc1->left >= lprcSrc1->right || lprcSrc1->top >= lprcSrc1->bottom ||
            lprcSrc2->left >= lprcSrc2->right || lprcSrc2->top >= lprcSrc2->bottom;
    if (empty || left >= right || top >= bottom) {
        lprcDst->left = 0;
        lprcDst->top = 0;
        lprcDst->right = 0;
        lprcDst->bottom = 0;
        return FALSE;
    }

    lprcDst->left = left;
    lprcDst->top = top;
    lprcDst->right = right;
    lprcDst->bottom = bottom;
    return TRUE;
}
