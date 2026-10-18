/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getdesktopwindow
 * RT-CASES:  the desktop window; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, GetDesktopWindow)
{
    return RT_HWND_DESKTOP;
}
