/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadid
 * RT-CASES:  returns the thread id stored in the thread block at +0x48; the id of the first thread is a nonzero multiple of 4; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, DWORD, GetCurrentThreadId)
{
    return LD32(ctx->gs_base + RT_TEB_THREAD_ID);
}
