/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-enumwindows
 * RT-CASES:  no windows: callback never called and TRUE; callback sees each window, newest first, with lParam; callback returning 0 stops the enumeration and returns FALSE; success leaves last error alone
 * RT-TRAPS:  more than 256 top-level windows; lpEnumFunc NULL
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, BOOL, EnumWindows, WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
    HWND list[256];
    unsigned n;

    if (!lpEnumFunc) RT_UNIMPLEMENTED(ctx, "EnumWindows with lpEnumFunc NULL");
    n = rt_window_list_top(list, 256);
    if (n > 256) RT_UNIMPLEMENTED(ctx, "EnumWindows with more than 256 top-level windows (%u)", n);
    for (unsigned i = 0; i < n; i++) {
        if (!(BOOL)GUEST_CALL(ctx, lpEnumFunc, list[i], lParam))
            return FALSE;
    }
    return TRUE;
}
