/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-releasedc
 * RT-CASES:  releases a context from GetDC once, a second release fails; the window is not compared; NULL and a context GetDC did not give out fail
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(user32, INT, ReleaseDC, HWND hWnd, HDC hDC)
{
    /* 1 when the context is released, 0 when it is not; the core does not compare the window,
     * and sets no last error. */
    return rt_dc_release(hWnd, hDC);
}
