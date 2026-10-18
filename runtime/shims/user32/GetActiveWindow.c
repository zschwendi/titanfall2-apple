/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getactivewindow
 * RT-CASES:  NULL until a window has been activated; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, GetActiveWindow)
{
    return rt_window_active();
}
