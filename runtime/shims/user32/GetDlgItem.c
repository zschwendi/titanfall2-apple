/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getdlgitem
 * RT-CASES:  returns the child whose identifier is the id; the children's own children are not searched; an identifier no child has gives NULL and ERROR_CONTROL_ID_NOT_FOUND; an invalid window gives NULL and ERROR_INVALID_WINDOW_HANDLE; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWND, GetDlgItem, HWND hDlg, INT nIDDlgItem)
{
    /* The core finds the child, sets ERROR_CONTROL_ID_NOT_FOUND or ERROR_INVALID_WINDOW_HANDLE
     * when it fails, and leaves last error alone on success. */
    return rt_window_child_by_id(hDlg, nIDDlgItem);
}
