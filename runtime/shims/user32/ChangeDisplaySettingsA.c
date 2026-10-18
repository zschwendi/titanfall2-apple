/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-changedisplaysettingsa
 * RT-CASES:  return to the default mode (NULL, 0); does not change last error
 * RT-TRAPS:  a display mode change
 */
#include "rt/shim.h"
#include "rt/win/user.h"

SHIM(user32, LONG, ChangeDisplaySettingsA, LPCVOID lpDevMode, DWORD dwFlags)
{
    /* The one case this runtime answers: no mode and no flags is the return to the default
     * mode, which changes nothing. A mode change is never guessed: the run stops with its flags. */
    if (lpDevMode == NULL && dwFlags == 0) return DISP_CHANGE_SUCCESSFUL;
    RT_UNIMPLEMENTED(ctx, "a display mode change (flags 0x%x)", dwFlags);
}
