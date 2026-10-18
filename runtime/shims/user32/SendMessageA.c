/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-sendmessagea
 * RT-CASES:  sends a message to a window of the calling thread and returns the procedure's result; an invalid window returns 0 with ERROR_INVALID_WINDOW_HANDLE
 * RT-TRAPS:  core: HWND_BROADCAST; core: RT_HWND_DESKTOP; core: text message of the other character type
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LRESULT, SendMessageA, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    /* The core sends to a window of the calling thread as an ANSI window (unicode false).
     * Its stops (HWND_BROADCAST, RT_HWND_DESKTOP, a text message of the other character type)
     * are not guarded here. */
    return rt_window_send(ctx, hWnd, Msg, wParam, lParam, false);
}
