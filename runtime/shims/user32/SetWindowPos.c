/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowpos
 * RT-CASES:  moves and resizes a window, seen by GetWindowRect; SWP_NOMOVE keeps the position and SWP_NOSIZE the size; a resize sends WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_WINDOWPOSCHANGED and WM_SIZE, and no WM_MOVE; nothing changed gives TRUE with only WM_WINDOWPOSCHANGING sent; SWP_NOSENDCHANGING sends no WM_WINDOWPOSCHANGING; an invalid window returns FALSE with ERROR_INVALID_WINDOW_HANDLE
 * RT-TRAPS:  core: RT_HWND_DESKTOP; core: flag bits Windows does not define; core: SWP_SHOWWINDOW; core: SWP_HIDEWINDOW for a shown window; core: HWND_TOPMOST and HWND_NOTOPMOST; core: an after that is no window; core: a child without SWP_NOACTIVATE; core: an activation while another window is active; core: a procedure that changes the flags or the order in WM_WINDOWPOSCHANGING; core: a frame rt_window_frame refuses
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, SetWindowPos, HWND hWnd, HWND hWndInsertAfter, INT X, INT Y, INT cx, INT cy, UINT uFlags)
{
    /* The core checks the window, sends the messages and sets last error itself when it fails
     * (rt/window.h, rt_window_set_pos).  Its stops are not guarded here. */
    return rt_window_set_pos(ctx, hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}
