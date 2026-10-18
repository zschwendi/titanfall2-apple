/* RT-STATUS: advisory
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow
 * RT-CASES:  every process id returns TRUE; the debug line is logged once; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(user32, BOOL, AllowSetForegroundWindow, DWORD dwProcessId)
{
    /* The call only grants another process the right to set the foreground window. This machine
     * runs no other process, so there is nothing to grant: the answer is TRUE for every argument,
     * ASFW_ANY ((DWORD)-1) included, and last error is left alone. The line is logged once. */
    rt_log_once(RT_CH_WINDOW, RT_LL_DEBUG, "user32.dll!AllowSetForegroundWindow",
                "AllowSetForegroundWindow(%u): no other process exists", dwProcessId);
    return TRUE;
}
