/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-openclipboard
 * RT-CASES:  opens the closed clipboard for a NULL owner; the same owner opens it again; an owner that is no window fails with ERROR_INVALID_WINDOW_HANDLE; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, OpenClipboard, HWND hWndNewOwner)
{
    return rt_clipboard_open(hWndNewOwner);
}
