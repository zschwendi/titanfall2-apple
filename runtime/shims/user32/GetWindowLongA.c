/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getwindowlonga
 * RT-CASES:  the style given at creation; the user data, the low 32 bits sign-extended; extra bytes at offsets 0 and 12 of 16; an index beyond the extra bytes; index -99; the pointer indexes GWLP_WNDPROC, GWLP_HINSTANCE and GWLP_HWNDPARENT are refused; a success leaves last error alone; a handle that is no window
 * RT-TRAPS:  core: the desktop window
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, LONG, GetWindowLongA, HWND hWnd, INT nIndex)
{
    /* The core does all of it: size 4, the ANSI character type, and it sets last error when it
     * fails and stops for what it does not answer (rt/window.h, rt_window_get_long). */
    return (LONG)rt_window_get_long(hWnd, nIndex, 4, false);
}
