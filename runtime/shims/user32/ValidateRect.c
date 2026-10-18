/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-validaterect
 * RT-CASES:  a record with a rectangle and with NULL; a handle that is no window
 * RT-TRAPS:  hWnd NULL
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, ValidateRect, HWND hWnd, LPCRECT lpRect)
{
    /* The core answers TRUE for a NULL window, and its result on Windows is not settled (rt/window.h, rt_window_validate; review S19): not guessed. */
    if (!hWnd) RT_UNIMPLEMENTED(ctx, "ValidateRect with hWnd NULL");
    /* The core does the rest, sets last error when it fails and leaves it alone on success. */
    return rt_window_validate(hWnd, lpRect);
}
