/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-windowfrompoint
 * RT-CASES:  points inside the desktop give the desktop window; points outside the desktop give NULL; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, WindowFromPoint, POINT Point)
{
    /* No window is ever visible in this runtime (rt/window.h, NO SCREEN), so the only thing
     * under a point is the desktop, which covers rt_desktop()->monitor: left and top are
     * inside, right and bottom are outside. Any other point gives NULL. Sets no last error. */
    const RtDesktop *d = rt_desktop();
    if (Point.x >= d->monitor.left && Point.x < d->monitor.right &&
        Point.y >= d->monitor.top && Point.y < d->monitor.bottom)
        return RT_HWND_DESKTOP;
    return NULL;
}
