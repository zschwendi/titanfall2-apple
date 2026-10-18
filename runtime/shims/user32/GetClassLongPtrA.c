/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getclasslongptra
 * RT-CASES:  returns the class field of a window: GCLP_HCURSOR as registered; GCW_ATOM is the atom of the registration; GCL_CBWNDEXTRA is the registered extra size (16); index -99 fails with ERROR_INVALID_INDEX; an invalid window gives 0; success leaves last error alone
 * RT-TRAPS:  core: GCLP_MENUNAME; core: RT_HWND_DESKTOP
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, ULONG_PTR, GetClassLongPtrA, HWND hWnd, INT nIndex)
{
    /* The core answers every index it supports and sets last error itself on failure. */
    return rt_wndclass_get_long(hWnd, nIndex, false);
}
