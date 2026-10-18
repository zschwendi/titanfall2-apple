/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-peekmessagea
 * RT-CASES:  FALSE at once on an empty queue, the MSG untouched; PM_NOREMOVE sees a posted message and leaves it; PM_REMOVE takes it; a range that excludes the message; messages come out in the order posted; (HWND)-1 takes a thread message; a filter that is not a window fails with ERROR_INVALID_WINDOW_HANDLE; does not change last error on success or on an empty queue
 * RT-TRAPS:  core: a NULL MSG pointer; core: a flag other than PM_REMOVE and PM_NOYIELD; core: window filter 1 or 0xFFFF; core: a range above 0xFFFF or with its first above its last; core: a posted WM_QUIT that the range leaves out
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, PeekMessageA, LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    /* The core has the queue, sets last error when the filter is not a window, and stops for
     * what it does not answer (rt/window.h, rt_msg_peek). */
    return rt_msg_peek(ctx, lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, false);
}
