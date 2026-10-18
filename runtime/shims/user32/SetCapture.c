/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setcapture
 * RT-CASES:  captures the mouse for a window and returns NULL when nothing was captured; capturing again with the window that holds the capture returns it; does not change last error
 * RT-TRAPS:  core: a handle that is not a window; core: a capture change while another window holds it
 */
#include "rt/shim.h"
#include "rt/host.h"
#include "rt/window.h"

/* A window of another thread: NULL with ERROR_ACCESS_DENIED, nothing changed.  That case is the
 * core's (rt/window.h says where the answer comes from; runtime/tests/core/win_state.c, plan 14). */
SHIM(user32, HWND, SetCapture, HWND hWnd)
{
    HWND prev = rt_window_set_capture(hWnd);
    /* The game holds the mouse for looking around: the host window's events go relative. */
    if (rt_host_window_wanted() && rt_window_is_host_window(hWnd))
        rt_host_set_relative(true);
    return prev;
}
