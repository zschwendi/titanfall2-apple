/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setcursor
 * RT-CASES:  sets a system cursor and returns the previous one, NULL at first; NULL removes the cursor and returns the previous one; does not change last error on success
 * RT-TRAPS:  core: a handle that is no cursor; core: a system cursor of an id that is not an IDC_ value
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HCURSOR, SetCursor, HCURSOR hCursor)
{
    /* The core makes the cursor and returns the previous one.  It stops on any handle that is
     * neither NULL nor a system cursor of an IDC_ value: no other cursor can exist here, so the
     * shim adds no guard for it. */
    return rt_cursor_set(hCursor);
}
