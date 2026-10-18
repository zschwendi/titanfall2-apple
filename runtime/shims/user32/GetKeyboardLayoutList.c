/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getkeyboardlayoutlist
 * RT-CASES:  nBuff 0 gives the count; nBuff 2 with a list gets the layout first; does not change last error
 * RT-TRAPS:  negative nBuff; NULL list with nBuff
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, INT, GetKeyboardLayoutList, INT nBuff, HKL *lpList)
{
    /* One layout (RUNTIME.md 8.4): nBuff 0 asks for the count and touches no list; a list gets the
     * layout in its first entry. A negative count and a buffer with no list are stops. */
    if (nBuff < 0) RT_UNIMPLEMENTED(ctx, "negative nBuff %d", nBuff);
    if (nBuff == 0) return 1;
    if (!lpList) RT_UNIMPLEMENTED(ctx, "NULL list with nBuff %d", nBuff);
    lpList[0] = RT_HKL_US;
    return 1;
}
