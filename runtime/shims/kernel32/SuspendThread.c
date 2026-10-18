/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-suspendthread
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"

SHIM(kernel32, DWORD, SuspendThread, HANDLE hThread)
{
    RT_UNIMPLEMENTED(ctx, "SuspendThread(%p)", hThread);
}
