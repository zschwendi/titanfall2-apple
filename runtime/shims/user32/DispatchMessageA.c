/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-dispatchmessagea
 * RT-CASES:  a message for a window reaches its procedure; a message with no window returns 0; a handle that is no window fails with ERROR_INVALID_WINDOW_HANDLE
 * RT-TRAPS:  core: msg NULL; core: WM_TIMER with a callback
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LRESULT, DispatchMessageA, const MSG *lpMsg)
{
    /* The core does all of it: msg->hwnd NULL answers 0, an invalid window fails with its
     * last error, and it stops for a NULL message and for WM_TIMER with a callback (rt/window.h). */
    return rt_window_dispatch(ctx, lpMsg, false);
}
