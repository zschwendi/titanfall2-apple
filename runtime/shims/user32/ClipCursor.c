/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-clipcursor
 * RT-CASES:  a rectangle becomes the clip and moves a cursor outside it inside; NULL restores the whole screen; an inverted rectangle is refused with ERROR_ACCESS_DENIED and changes nothing; success leaves last error alone
 * RT-TRAPS:  core: an empty rectangle, not inverted (left and right equal); core: an empty rectangle, not inverted (top and bottom equal); core: a rectangle with no point on the screen
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, ClipCursor, LPCRECT lpRect)
{
    /* The core refuses an inverted rectangle with ERROR_ACCESS_DENIED and sets last error
     * itself; it stops on the empty and off-screen rectangles listed above. */
    return rt_cursor_clip(lpRect);
}
