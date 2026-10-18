/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-fatalappexita
 * RT-CASES:  prints the message and ends the process with status 3; a NULL message prints (null)
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, void, FatalAppExitA, UINT uAction, LPCSTR lpMessageText)
{
    /* Never returns and never fails: the core writes the fatal report, flushes the log and
     * ends the process.  uAction is printed, never checked; a NULL message prints (null). */
    rt_fatal_app_exit(ctx, uAction, lpMessageText);
}
