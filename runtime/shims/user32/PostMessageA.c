/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-postmessagea
 * RT-CASES:  a post with a NULL window is a thread message with the four values; application and registered messages are posted as they are; what is not a window fails with ERROR_INVALID_WINDOW_HANDLE and queues nothing; does not change last error on success
 * RT-TRAPS:  core: HWND_BROADCAST; core: (HWND)-1; core: the desktop window; core: a system message outside the short list; core: a message above 0xFFFF; core: a character message with a character of 0x80 or above
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, PostMessageA, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    /* The core finds the queue of the window's thread, wakes it, and stops for the messages
     * and windows it does not answer (rt/window.h, rt_msg_post). */
    return rt_msg_post(ctx, hWnd, Msg, wParam, lParam);
}
