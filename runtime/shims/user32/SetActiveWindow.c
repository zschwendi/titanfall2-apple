/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setactivewindow
 * RT-CASES:  NULL: returns NULL, last error untouched; a handle that is not a window: NULL with ERROR_INVALID_WINDOW_HANDLE
 * RT-TRAPS:  core: a valid window (activation needs a visible window)
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, SetActiveWindow, HWND hWnd)
{
    return rt_window_set_active(hWnd);
}
