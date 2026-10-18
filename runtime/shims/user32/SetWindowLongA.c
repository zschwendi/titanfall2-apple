/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowlonga
 * RT-CASES:  the style, the extended style and the user data set and returned; the previous value of a set; extra bytes at offsets 0 and 12 of 16; an index beyond the extra bytes; index -99; the pointer indexes GWLP_WNDPROC, GWLP_HINSTANCE and GWLP_HWNDPARENT are refused; a set whose previous value is 0 leaves last error alone; a handle that is no window
 * RT-TRAPS:  core: the desktop window; core: GWL_STYLE that would set WS_VISIBLE; core: GWL_STYLE or GWL_EXSTYLE that would change a frame bit
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LONG, SetWindowLongA, HWND hWnd, INT nIndex, LONG dwNewLong)
{
    /* The core does all of it: size 4, the ANSI character type, and it sets last error when it
     * fails and stops for what it does not answer (rt/window.h, rt_window_set_long). */
    return (LONG)rt_window_set_long(hWnd, nIndex, dwNewLong, 4, false);
}
