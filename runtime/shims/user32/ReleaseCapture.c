/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-releasecapture
 * RT-CASES:  releases the capture and returns TRUE; returns TRUE when nothing was captured; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/host.h"
#include "rt/window.h"

SHIM(user32, BOOL, ReleaseCapture)
{
    BOOL ok = rt_window_release_capture();
    if (rt_host_window_wanted() && rt_window_host_open())
        rt_host_set_relative(false);
    return ok;
}
