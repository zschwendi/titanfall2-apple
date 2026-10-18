/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-IsIconic
 * RT-CASES:  a new record; after the window is shown; the desktop window; a handle that is no window
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, IsIconic, HWND hWnd)
{
    /* The core answers every case and sets last error itself when it fails (rt/window.h, rt_window_is_iconic). */
    return rt_window_is_iconic(hWnd);
}
