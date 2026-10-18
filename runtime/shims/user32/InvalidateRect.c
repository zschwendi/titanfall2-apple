/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-invalidaterect
 * RT-CASES:  a record with a rectangle and with NULL, erase TRUE or FALSE; a NULL window invalidates all windows; a handle that is no window
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, InvalidateRect, HWND hWnd, LPCRECT lpRect, BOOL bErase)
{
    /* The core does all of it, sets last error when it fails and leaves it alone on success (rt/window.h, rt_window_invalidate). */
    return rt_window_invalidate(hWnd, lpRect, bErase);
}
