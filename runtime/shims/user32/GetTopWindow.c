/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-gettopwindow
 * RT-CASES:  NULL and the desktop give the newest top-level window; a parent's top window is its first child created; a window with no children and no such window are NULL and leave last error alone; an invalid window is NULL with ERROR_INVALID_WINDOW_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, GetTopWindow, HWND hWnd)
{
    return rt_window_top(hWnd);
}
