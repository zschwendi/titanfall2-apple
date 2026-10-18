/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowlongptrw
 * RT-CASES:  replacing the procedure returns the one before, and the next replacement returns the one replaced; the user data and the extra bytes; an index that does not exist; a handle that is no window
 * RT-TRAPS:  core: a style that would show the window; core: a style that changes the frame; core: the parent of a window (GWLP_HWNDPARENT); core: the desktop window; core: the procedure of a window of the other character type
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LONG_PTR, SetWindowLongPtrW, HWND hWnd, INT nIndex, LONG_PTR dwNewLong)
{
    /* The core answers, sets last error when it fails, and stops for the cases it does not answer. */
    return rt_window_set_long(hWnd, nIndex, dwNewLong, 8, true);
}
