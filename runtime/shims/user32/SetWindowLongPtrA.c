/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowlongptra
 * RT-CASES:  stores a 64-bit user data and returns the previous value; stores 8 extra bytes at offset 8 and returns the previous value; fails with ERROR_INVALID_INDEX for an index past the extra bytes or an unknown negative index, changing nothing; sets the window procedure of an A window and returns the previous one; a procedure value of 0 changes nothing and returns the procedure; sets the style when that does not show the window or change its frame; does not change last error on success; returns 0 for a handle that is not a window
 * RT-TRAPS:  core: GWLP_HWNDPARENT; core: GWLP_WNDPROC of a window of the other character type; core: GWL_STYLE setting WS_VISIBLE; core: GWL_STYLE changing a frame bit; core: GWL_EXSTYLE changing a frame bit; core: RT_HWND_DESKTOP
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LONG_PTR, SetWindowLongPtrA, HWND hWnd, INT nIndex, LONG_PTR dwNewLong)
{
    return rt_window_set_long(hWnd, nIndex, dwNewLong, 8, false);
}
