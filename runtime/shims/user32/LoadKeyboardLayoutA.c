/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-loadkeyboardlayouta
 * RT-CASES:  the name 00000409: returns RT_HKL_US; does not change last error
 * RT-TRAPS:  LoadKeyboardLayoutA with name
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HKL, LoadKeyboardLayoutA, LPCSTR pwszKLID, UINT Flags)
{
    /* The name must be exactly the eight characters of the US layout, "00000409". Any other
     * name, and NULL, is not a case this runtime answers, so the run stops with the name.
     * Flags is not used. */
    (void)Flags;
    if (!pwszKLID || strcmp(pwszKLID, "00000409") != 0)
        RT_UNIMPLEMENTED(ctx, "LoadKeyboardLayoutA with name %s", pwszKLID ? pwszKLID : "NULL");
    return RT_HKL_US;
}
