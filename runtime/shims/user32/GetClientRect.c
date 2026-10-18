/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getclientrect
 * RT-CASES:  the client area: the window rectangle less its frame, from 0, 0; the desktop window gives the monitor rectangle; a handle that is no window: the rectangle is not written; does not change last error
 * RT-TRAPS:  core: a NULL rectangle
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, GetClientRect, HWND hWnd, LPRECT lpRect)
{
    /* The core does all of it: it sets last error when it fails, leaves it alone on success, and stops for a NULL rectangle (rt/window.h, rt_window_get_client_rect). */
    return rt_window_get_client_rect(hWnd, lpRect);
}
