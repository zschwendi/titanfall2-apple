/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-showwindow
 * RT-CASES:  SW_HIDE on a hidden window: FALSE, nothing sent; SW_SHOWDEFAULT on a hidden window: shows and activates it; SW_SHOWNORMAL, SW_SHOW, SW_SHOWDEFAULT on a shown window: nonzero, nothing sent; SW_HIDE on the shown window: nonzero, hides and deactivates it; a handle that is no window
 * RT-TRAPS:  core: command 3 (SW_SHOWMAXIMIZED); core: a child window; core: an activation while another window is active
 */
#include "rt/shim.h"
#include "rt/window.h"
#include "rt/win/user.h"

SHIM(user32, BOOL, ShowWindow, HWND hWnd, INT nCmdShow)
{
    /* The core returns whether the window was shown before the call, and sets last error
     * itself when it fails (rt/window.h, rt_window_show).  A command or window the core
     * does not build STOPS in the core. */
    BOOL was = rt_window_show(hWnd, nCmdShow);
    /* The game's top-level window, once shown, gets its host window (RT_HOST_WINDOW; rt/window.h). */
    if (rt_host_window_wanted() && rt_window_is_visible(hWnd)) {
        LONG_PTR style = rt_window_get_long(hWnd, GWL_STYLE, 4, false);
        if (!(style & WS_CHILD))
            (void)rt_window_host_layer(hWnd);
    }
    return was;
}
