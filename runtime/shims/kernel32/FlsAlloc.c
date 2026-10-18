/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fibersapi/nf-fibersapi-flsalloc
 * RT-CASES:  two allocations give two different indexes, neither 0, and leave last error alone; a freed index is given out again
 * RT-TRAPS:  core: all 127 indexes allocated
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, DWORD, FlsAlloc, PFLS_CALLBACK_FUNCTION lpCallback)
{
    /* The core gives the lowest free index from 1, with lpCallback recorded for it (rt/thread.h),
     * and sets nothing on success.  The page's failure, FLS_OUT_OF_INDEXES, is what Windows
     * returns when no index is free; the core stops (fatal) when all 127 are allocated instead,
     * and the shim adds no guard for it. */
    return rt_fls_alloc(lpCallback);
}
