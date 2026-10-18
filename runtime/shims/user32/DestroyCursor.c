/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-destroycursor
 * RT-CASES:  a system cursor (RT_HCURSOR_SYSTEM of an IDC_ value): returns TRUE, the cursor stays usable; does not change last error
 * RT-TRAPS:  not a system cursor
 */
#include "rt/shim.h"
#include "rt/window.h"

/* TRUE when h is RT_HCURSOR_SYSTEM of one of the IDC_ values in rt/win/user.h: 32512 to 32516,
 * 32642 to 32646, 32648 to 32651. */
static int is_system_cursor(HCURSOR h)
{
    uintptr_t v = (uintptr_t)h;
    unsigned id = (unsigned)(v & 0xFFFFu);

    if ((HCURSOR)(uintptr_t)v != RT_HCURSOR_SYSTEM(id)) return 0;
    return (id >= IDC_ARROW && id <= IDC_UPARROW)
        || (id >= IDC_SIZENWSE && id <= IDC_SIZEALL)
        || (id >= IDC_NO && id <= IDC_HELP);
}

SHIM(user32, BOOL, DestroyCursor, HCURSOR hCursor)
{
    /* A system cursor is shared and is not destroyed here: it stays the cursor it was, and
     * last error is left alone. Any other handle is not a case this runtime answers, so the
     * run stops with the handle. */
    if (!is_system_cursor(hCursor))
        RT_UNIMPLEMENTED(ctx, "DestroyCursor of %p, which is not a system cursor: no other cursor "
                              "can exist here", (void *)hCursor);
    return TRUE;
}
