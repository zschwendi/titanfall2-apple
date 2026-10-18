/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getdc
 * RT-CASES:  NULL gives a display context of the screen; a handle that is not a window gives NULL with ERROR_INVALID_WINDOW_HANDLE
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(user32, HDC, GetDC, HWND hWnd)
{
    /* The core returns NULL with ERROR_INVALID_WINDOW_HANDLE for a handle that is not a window,
     * and sets no other last error. */
    return rt_dc_get(hWnd);
}
