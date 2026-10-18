/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setcursorpos
 * RT-CASES:  moves the cursor to the given position; clamps a position outside the screen to the nearest point inside; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, SetCursorPos, INT X, INT Y)
{
    /* The core clamps the position to the clip rectangle and always succeeds. */
    return rt_cursor_set_pos(X, Y);
}
