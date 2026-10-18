/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-activatekeyboardlayout
 * RT-CASES:  RT_HKL_US, HKL_NEXT or HKL_PREV: the layout stays, returns RT_HKL_US; does not change last error
 * RT-TRAPS:  ActivateKeyboardLayout with handle
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HKL, ActivateKeyboardLayout, HKL hkl, UINT Flags)
{
    /* The one loaded layout is RT_HKL_US. HKL_NEXT (1) and HKL_PREV (0) name it too, so the
     * layout stays what it is and the previous layout is RT_HKL_US. Any other handle is not
     * a case this runtime answers, so the run stops with the handle. Flags is not used. */
    (void)Flags;
    if (hkl != RT_HKL_US && hkl != (HKL)1 && hkl != (HKL)0)
        RT_UNIMPLEMENTED(ctx, "ActivateKeyboardLayout with handle %p", (void *)hkl);
    return RT_HKL_US;
}
