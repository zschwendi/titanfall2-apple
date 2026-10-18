/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getforegroundwindow
 * RT-CASES:  NULL before a window is activated, on two calls; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, GetForegroundWindow)
{
    /* The core answers from the activation state and cannot fail: no last error is set. */
    return rt_window_foreground();
}
