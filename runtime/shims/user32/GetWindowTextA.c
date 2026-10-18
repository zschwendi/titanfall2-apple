/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getwindowtexta
 * RT-CASES:  the text set on the window; a buffer of 4 gets 3 characters and a terminator; cap 0 returns 0 and leaves the buffer alone; a window that does not exist returns 0 with ERROR_INVALID_WINDOW_HANDLE; a success leaves last error alone
 * RT-TRAPS:  core: a window of the other character type
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, INT, GetWindowTextA, HWND hWnd, LPSTR lpString, INT nMaxCount)
{
    /* The core sends WM_GETTEXT to the window's procedure, sets last error when the window is
     * invalid, and stops for a window of the other character type (rt/window.h). */
    return rt_window_get_text(ctx, hWnd, lpString, nMaxCount, false);
}
