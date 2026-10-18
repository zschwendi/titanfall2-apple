/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getclipboarddata
 * RT-CASES:  NULL with ERROR_NOT_FOUND for a format that is not set; the handle set for a format while the clipboard is open; success does not change last error; NULL with ERROR_CLIPBOARD_NOT_OPEN when the clipboard is not open
 * RT-TRAPS:  core: get of CF_TEXT when only CF_UNICODETEXT is set
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HANDLE, GetClipboardData, UINT uFormat)
{
    /* The core checks that the clipboard is open by this thread, finds the handle for the format,
     * and sets last error itself when it fails. */
    return rt_clipboard_get(uFormat);
}
