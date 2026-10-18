/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getwindowlongptra
 * RT-CASES:  reads the user data as a 64-bit value; reads 8 extra bytes at offset 8 of a 16-byte area whole; fails with ERROR_INVALID_INDEX for an index past the extra bytes or an unknown negative index; reads the style, extended style and identifier; reads the window procedure of an A window; reads the parent of a child and 0 for a top-level window; returns 0 for a handle that is not a window; does not change last error on success
 * RT-TRAPS:  core: GWLP_WNDPROC of a window of the other character type; core: RT_HWND_DESKTOP
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LONG_PTR, GetWindowLongPtrA, HWND hWnd, INT nIndex)
{
    return rt_window_get_long(hWnd, nIndex, 8, false);
}
