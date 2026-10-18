/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-destroywindow
 * RT-CASES:  destroys a window and invalidates its handle; fails with ERROR_INVALID_WINDOW_HANDLE for NULL, a free slot and a destroyed window; returns TRUE and leaves last error alone; sends WM_DESTROY to a window, its children and grandchildren, then WM_NCDESTROY in post-order; sends WM_PARENTNOTIFY to the parent of a child destroyed by itself and none for children destroyed with their parent; destroys owned windows completely before the owner's WM_DESTROY; hides a shown window before destroying it
 * RT-TRAPS:  core: RT_HWND_DESKTOP; core: a window that gets a new child while it is destroyed
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, DestroyWindow, HWND hWnd)
{
    /* The core sends the messages, destroys the owned windows and the children, and sets last
     * error on failure; the STOPS of rt_window_destroy are left to the core. */
    return rt_window_destroy(ctx, hWnd);
}
