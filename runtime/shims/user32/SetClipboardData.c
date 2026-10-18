/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setclipboarddata
 * RT-CASES:  returns the handle and keeps it for the format while the clipboard is open; setting a format that is set replaces its handle; NULL with ERROR_CLIPBOARD_NOT_OPEN for format 0 although the clipboard is open; success does not change last error; NULL with ERROR_CLIPBOARD_NOT_OPEN when the clipboard is not open
 * RT-TRAPS:  core: set with hMem NULL (delayed rendering); core: set of a 17th format
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HANDLE, SetClipboardData, UINT uFormat, HANDLE hMem)
{
    /* The core checks that the clipboard is open by this thread, refuses format 0, and keeps the
     * handle for the format; it sets last error itself when it fails. */
    return rt_clipboard_set(uFormat, hMem);
}
