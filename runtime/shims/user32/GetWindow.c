/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getwindow
 * RT-CASES:  GW_HWNDNEXT and GW_HWNDPREV walk the top-level windows newest first; GW_CHILD is the first child created, and GW_HWNDNEXT walks the children in creation order; GW_OWNER is the owner of an owned window and NULL for one with no owner; GW_ENABLEDPOPUP is NULL and leaves last error alone; no such window is NULL and leaves last error alone; an invalid window is NULL with ERROR_INVALID_WINDOW_HANDLE; a command of 7 or more is NULL with ERROR_INVALID_PARAMETER
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, GetWindow, HWND hWnd, UINT uCmd)
{
    return rt_window_get(hWnd, uCmd);
}
