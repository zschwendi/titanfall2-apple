/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-unhookwindowshookex
 * RT-CASES:  removes a hook and returns TRUE; a second unhook of the same handle fails with ERROR_INVALID_HOOK_HANDLE; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, UnhookWindowsHookEx, HHOOK hhk)
{
    /* The core sets the last error when it fails and leaves it alone when it succeeds. */
    return rt_hook_remove(hhk);
}
