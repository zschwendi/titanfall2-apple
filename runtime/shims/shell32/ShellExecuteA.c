/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/shellapi/nf-shellapi-shellexecutea
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"

SHIM(shell32, HINSTANCE, ShellExecuteA, HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
{
    (void)hwnd;
    (void)lpDirectory;
    (void)nShowCmd;
    RT_UNIMPLEMENTED(ctx, "ShellExecuteA(operation \"%s\", file \"%s\", parameters \"%s\")",
                     lpOperation ? lpOperation : "(null)", lpFile ? lpFile : "(null)",
                     lpParameters ? lpParameters : "(null)");
}
