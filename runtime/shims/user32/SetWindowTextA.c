/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowtexta
 * RT-CASES:  sets the text the window's procedure receives and returns TRUE; the empty string clears the text; a success leaves last error alone; a window that does not exist returns FALSE with ERROR_INVALID_WINDOW_HANDLE
 * RT-TRAPS:  core: a window of the other character type
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, SetWindowTextA, HWND hWnd, LPCSTR lpString)
{
    /* The core sends WM_SETTEXT to the window's procedure, sets last error when the window is
     * invalid, and stops for a window of the other character type (rt/window.h). */
    return rt_window_set_text(ctx, hWnd, RT_TEXT_A(lpString));
}
