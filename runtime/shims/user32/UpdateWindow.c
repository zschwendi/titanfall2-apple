/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-updatewindow
 * RT-CASES:  TRUE for a record, nothing is painted; NULL and a handle that is no window
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, UpdateWindow, HWND hWnd)
{
    /* A record has no update region, so nothing is sent; a NULL or stale handle fails with
     * last error set by the core (rt/window.h, rt_window_update). */
    return rt_window_update(hWnd);
}
