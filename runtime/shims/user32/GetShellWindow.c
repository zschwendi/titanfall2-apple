/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getshellwindow
 * RT-CASES:  no shell process: NULL; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(user32, HWND, GetShellWindow)
{
    /* No shell process exists on this machine; NULL is the documented answer when there is
     * no shell window. */
    return NULL;
}
