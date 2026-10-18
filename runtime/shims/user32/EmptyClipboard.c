/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-emptyclipboard
 * RT-CASES:  empties an open clipboard and leaves no format; not open fails with ERROR_CLIPBOARD_NOT_OPEN and keeps the formats; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, EmptyClipboard)
{
    return rt_clipboard_empty();
}
