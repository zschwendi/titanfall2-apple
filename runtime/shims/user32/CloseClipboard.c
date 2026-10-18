/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-closeclipboard
 * RT-CASES:  fails with ERROR_CLIPBOARD_NOT_OPEN while the clipboard is closed; one close closes the clipboard that was opened twice; a second close fails with ERROR_CLIPBOARD_NOT_OPEN; does not change last error on success
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, CloseClipboard)
{
    return rt_clipboard_close();
}
