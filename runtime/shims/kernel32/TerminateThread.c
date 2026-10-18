/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminatethread
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"

SHIM(kernel32, BOOL, TerminateThread, HANDLE hThread, DWORD dwExitCode)
{
    RT_UNIMPLEMENTED(ctx, "TerminateThread(%p, %u)", hThread, dwExitCode);
}
