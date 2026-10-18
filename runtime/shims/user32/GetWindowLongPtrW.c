/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getwindowlongptrw
 * RT-CASES:  the procedure, the style, the user data and the extra bytes; an index that does not exist; a handle that is no window
 * RT-TRAPS:  core: the desktop window; core: the procedure of a window of the other character type
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LONG_PTR, GetWindowLongPtrW, HWND hWnd, INT nIndex)
{
    /* The core answers, sets last error when it fails, and stops for the cases it does not answer. */
    return rt_window_get_long(hWnd, nIndex, 8, true);
}
