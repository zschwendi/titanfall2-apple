/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-screentoclient
 * RT-CASES:  subtracts the client area's screen position from a point; the desktop window changes nothing; a handle that is no window: the point is not written; does not change last error
 * RT-TRAPS:  a NULL point
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, ScreenToClient, HWND hWnd, LPPOINT lpPoint)
{
    /* The documentation does not say what a NULL point does, so the shim refuses it. */
    if (!lpPoint) RT_UNIMPLEMENTED(ctx, "a NULL point (not documented)");
    /* The core does the conversion, sets last error when it fails and leaves it alone on success (rt/window.h, rt_window_screen_to_client). */
    return rt_window_screen_to_client(hWnd, lpPoint);
}
