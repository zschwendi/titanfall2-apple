/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getmessagea
 * RT-CASES:  a posted message returns 1 with its values; a posted WM_QUIT returns 0; a filter that is not a window returns -1 with ERROR_INVALID_WINDOW_HANDLE; does not change last error on success
 * RT-TRAPS:  core: a NULL MSG pointer; core: window filter 1 or 0xFFFF; core: a range above 0xFFFF or with its first above its last; core: a posted WM_QUIT that the range leaves out
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, GetMessageA, LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    /* The core waits for a post when the queue has nothing that passes (rt/window.h,
     * rt_msg_get): on a queue nobody posts to this does not return, as on Windows. */
    return rt_msg_get(ctx, lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, false);
}
