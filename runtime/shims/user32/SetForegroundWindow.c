/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setforegroundwindow
 * RT-CASES:  a NULL handle; a handle that is no window
 * RT-TRAPS:  core: a valid record
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, SetForegroundWindow, HWND hWnd)
{
    /* The core answers the two listed cases and sets last error itself when it fails
     * (rt/window.h, rt_window_set_foreground); a valid record STOPS in the core. */
    return rt_window_set_foreground(hWnd);
}
