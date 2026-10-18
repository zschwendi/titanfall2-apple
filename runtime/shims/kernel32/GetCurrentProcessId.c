/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocessid
 * RT-CASES:  returns the process id stored in the thread block at +0x40; the id is a nonzero multiple of 4; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, DWORD, GetCurrentProcessId)
{
    return LD32(ctx->gs_base + RT_TEB_PROCESS_ID);
}
