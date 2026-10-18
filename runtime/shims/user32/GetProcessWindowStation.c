/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getprocesswindowstation
 * RT-CASES:  returns the window station handle; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, HWINSTA, GetProcessWindowStation)
{
    /* The process has one window station, WinSta0: the core's constant (rt/window.h, RT_HWINSTA). */
    return RT_HWINSTA;
}
