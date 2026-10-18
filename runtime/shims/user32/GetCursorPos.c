/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getcursorpos
 * RT-CASES:  starts at the middle of the screen; reports the position last set by SetCursorPos; a NULL pointer fails with ERROR_NOACCESS; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, GetCursorPos, LPPOINT lpPoint)
{
    /* The core writes the position, or sets ERROR_NOACCESS itself for a NULL lpPoint. */
    return rt_cursor_get_pos(lpPoint);
}
