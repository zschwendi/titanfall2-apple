/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowtextw
 * RT-CASES:  sets the window text of a window of the calling thread and returns TRUE; the text is UTF-16 code units, not only ASCII; an empty string sets an empty text; an invalid window returns FALSE with ERROR_INVALID_WINDOW_HANDLE; success leaves last error alone
 * RT-TRAPS:  core: WM_SETTEXT through the W function to an A window
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, SetWindowTextW, HWND hWnd, LPCWSTR lpString)
{
    /* The core sends WM_SETTEXT to the window's procedure, sets last error itself when the
     * window is invalid, and stops for a window of the other character type (rt/window.h,
     * rt_window_set_text). */
    return rt_window_set_text(ctx, hWnd, RT_TEXT_W(lpString));
}
