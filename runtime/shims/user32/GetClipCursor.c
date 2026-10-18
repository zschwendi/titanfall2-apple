/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getclipcursor
 * RT-CASES:  the whole screen at first; the rectangle that ClipCursor set; the whole screen after ClipCursor with NULL; writes the RECT and nothing else; does not change last error
 * RT-TRAPS:  core: NULL lpRect
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, GetClipCursor, LPRECT lpRect)
{
    /* The core fills the RECT and returns TRUE; it stops on a NULL lpRect (listed above). */
    return rt_cursor_get_clip(lpRect);
}
